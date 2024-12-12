/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Nutanix Inc. All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/event.h"
#include "module/bdev/nvme/bdev_nvme.h"

struct bdev_context_t {
	char *bdev_name;
	struct spdk_bdev *bdev;
	struct spdk_bdev_desc *bdev_desc;
	struct spdk_io_channel *bdev_io_channel;
	char *buff;
	uint32_t buff_size;
	struct spdk_nvme_reservation_register_data rr_data;
	struct spdk_nvme_reservation_acquire_data cdata;
	struct spdk_nvme_reservation_key_data rdata;
	struct spdk_nvme_cmd cmd;
	uint64_t resv_key;
};

enum resv_state {
	REGISTER,
	ACQUIRE_SUCCESS,
	ACQUIRE_FAIL,
	PREEMPT_ACQUIRE,
	IO_SUCCESS,
	IO_FAIL,
	RELEASE,
	UNREGISTER_PRI_HOST,
	UNREGISTER_SEC_HOST
};

enum multi_host_resv_stage {
	PRI_HOST_RESV,
	SEC_HOST_RESV,
	PRI_HOST_RESV_PREEMPT,
	SEC_HOST_RESV_AFTER_PREEMPT,
	FINALIZE
};

enum host_config {
	SINGLE_HOST,
	MULTI_HOST
};

struct bdev_resv_ctx {
	struct bdev_context_t *bdev_ctx_pri_host;
	struct bdev_context_t *bdev_ctx_sec_host;
	const char *bdev_names[1];
	enum host_config host_type;
	enum resv_state bdev_resv_state;
	enum multi_host_resv_stage multi_host_pr_stage;
	bool cmd_success_expected;
};

static struct spdk_nvme_transport_id g_trid;
static struct spdk_thread *g_app_thread;
static char g_pri_hostnqn[SPDK_NVMF_NQN_MAX_LEN + 1];
static char g_sec_hostnqn[SPDK_NVMF_NQN_MAX_LEN + 1];
static bool io_success_expected;
static bool pr_test_on = false;
static struct bdev_context_t *g_bdev_context;
static struct spdk_env_opts g_env_opts;
static struct bdev_resv_ctx *g_bdev_pr_ctx;

#define EXT_HOST_ID	((uint8_t[]){0x0f, 0x97, 0xcd, 0x74, 0x8c, 0x80, 0x41, 0x42, \
				     0x99, 0x0f, 0x65, 0xc4, 0xf0, 0x39, 0x24, 0x20})
#define SEC_EXT_HOST_ID	((uint8_t[]){0xff, 0x99, 0xef, 0x77, 0x8d, 0x89, 0x44, 0x22, \
				     0x66, 0xee, 0x66, 0xcc, 0x00, 0x33, 0x22, 0x44})
#define KEY_1		0xDEADBEAF5A5A5A5B
#define KEY_2		0xDEADBEAF5A5A5A5A
#define NR_KEY		0xDEADBEAF5B5B5B5B

static void
exit_initiator(char *msg, int rc)
{
	if (rc) {
		SPDK_ERRLOG("%s: %s: %d\n", spdk_strerror(-rc), msg, -rc);
	} else {
		SPDK_NOTICELOG("Exiting the initiator as %s\n", msg);
	}
	spdk_thread_exit(g_app_thread);
}

static void reservation_request_cb_fn(struct spdk_bdev_io *bdev_io, bool success, void *ctx);

static void test_bdev_write(void *ctx, bool arg);

static void
nvmf_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
		   void *event_ctx)
{
	SPDK_NOTICELOG("unsupported bdev event: type %d\n", type);
}

static void
subsystem_init_done(int rc, void *arg)
{
	if (rc == 0) {
		SPDK_NOTICELOG("subsystem init is successful\n");
	} else {
		SPDK_ERRLOG("subsystem init failed\n");
	}
}

static void
bdev_nvme_update_subsystem_nqn(void *arg)
{
	struct spdk_bdev *bdev;
	struct spdk_nvme_ctrlr *nvme_ctrl;
	const struct spdk_nvme_ctrlr_data *cdata;

	bdev = arg;
	nvme_ctrl = bdev_nvme_get_ctrlr(bdev);
	cdata = spdk_nvme_ctrlr_get_data(nvme_ctrl);

	memcpy(g_trid.subnqn, cdata->subnqn, sizeof(g_trid.subnqn));
}

static void
usage(const char *program_name)
{
	printf("%s [options]", program_name);
	printf("\n");
	printf("options:\n");
	printf(" -r trid    remote NVMe over Fabrics target address\n");
	printf("    Format: 'key:value [key:value] ...'\n");
	printf("    Keys:\n");
	printf("     trtype      Transport type (e.g. TCP)\n");
	printf("     adrfam      Address family (e.g. IPv4, IPv6)\n");
	printf("     traddr      Transport address (e.g. 192.168.100.8)\n");
	printf("     trsvcid     Transport service identifier (e.g. 4420)\n");
	printf("     subnqn      Subsystem NQN (default: %s)\n", SPDK_NVMF_DISCOVERY_NQN);
	printf("     hostnqn     Host NQN\n");
	printf("    Example: -r 'trtype:RDMA adrfam:IPv4 traddr:192.168.100.8 trsvcid:4420'\n");

	printf(" -n         set no_huge to true\n");
	printf(" -q         secondary host nqn\n");
	printf(" -H         show this usage\n");
}

static int
parse_args(int argc, char **argv)
{
	int op;
	char *hostnqn;

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}
	spdk_nvme_trid_populate_transport(&g_trid, SPDK_NVME_TRANSPORT_TCP);
	snprintf(g_trid.subnqn, sizeof(g_trid.subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);

	while ((op = getopt(argc, argv, "nr:q:H")) != -1) {
		switch (op) {
		case 'n':
			g_env_opts.no_huge = true;
			g_env_opts.mem_size = 512;
			break;
		case 'r':
			if (spdk_nvme_transport_id_parse(&g_trid, optarg) != 0) {
				fprintf(stderr, "error parsing transport address\n");
				return 1;
			}

			assert(optarg != NULL);
			hostnqn = strcasestr(optarg, "hostnqn:");
			if (hostnqn) {
				size_t len;
				hostnqn += strlen("hostnqn:");
				len = strcspn(hostnqn, " \t\n");
				if (len > (sizeof(g_pri_hostnqn) - 1)) {
					fprintf(stderr, "host NQN is too long\n");
					return 1;
				}
				memcpy(g_pri_hostnqn, hostnqn, len);
				g_pri_hostnqn[len] = '\0';
			}
			break;
		case 'q':
			size_t len;
			len = strcspn(optarg, " \t\n");
			if (len > (sizeof(g_sec_hostnqn) - 1)) {
				fprintf(stderr, "host NQN is too long\n");
				return 1;
			}
			memcpy(g_sec_hostnqn, optarg, len);
			g_sec_hostnqn[len] = '\0';
			break;
		case 'H':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
		default:
			usage(argv[0]);
			return 1;
		}
	}
	return 0;
}

static void
bdev_reservation_register(void *ctx, enum spdk_nvme_reservation_register_action opc, bool success)
{
	int rc = 0;
	struct bdev_context_t *bdev_ctx = ctx;

	memset(&(bdev_ctx->cmd), 0, sizeof(bdev_ctx->cmd));
	memset(&(bdev_ctx->rr_data), 0, sizeof(bdev_ctx->rr_data));
	g_bdev_pr_ctx->cmd_success_expected = success;

	if (opc == SPDK_NVME_RESERVE_REGISTER_KEY) {
		bdev_ctx->rr_data.crkey = 0;
		bdev_ctx->rr_data.nrkey = bdev_ctx->resv_key;
	} else if (opc == SPDK_NVME_RESERVE_UNREGISTER_KEY) {
		bdev_ctx->rr_data.crkey = bdev_ctx->resv_key;
		bdev_ctx->rr_data.nrkey = 0;
	} else if (opc == SPDK_NVME_RESERVE_REPLACE_KEY) {
		bdev_ctx->rr_data.crkey = bdev_ctx->resv_key;
		bdev_ctx->rr_data.nrkey = NR_KEY;
	}

	bdev_ctx->cmd.opc = SPDK_NVME_OPC_RESERVATION_REGISTER;
	bdev_ctx->cmd.cdw10_bits.resv_register.rrega = opc;
	bdev_ctx->cmd.cdw10_bits.resv_register.iekey = false;
	bdev_ctx->cmd.cdw10_bits.resv_register.cptpl = SPDK_NVME_RESERVE_PTPL_CLEAR_POWER_ON;
	rc = spdk_bdev_nvme_io_passthru(bdev_ctx->bdev_desc, bdev_ctx->bdev_io_channel,
					&bdev_ctx->cmd, &bdev_ctx->rr_data, sizeof(bdev_ctx->rr_data),
					reservation_request_cb_fn, NULL);
	if (rc) {
		exit_initiator("failed to submit NVMe I/O command to bdev", rc);
		return;
	}
}

static void
bdev_reservation_acquire(void *ctx, enum spdk_nvme_reservation_acquire_action opc, bool success)
{
	int rc = 0;
	struct bdev_context_t *bdev_ctx = ctx;

	memset(&(bdev_ctx->cmd), 0, sizeof(bdev_ctx->cmd));
	memset(&(bdev_ctx->cdata), 0, sizeof(bdev_ctx->cdata));
	g_bdev_pr_ctx->cmd_success_expected = success;

	bdev_ctx->cdata.crkey = bdev_ctx->resv_key;
	bdev_ctx->cdata.prkey = KEY_2;
	bdev_ctx->cmd.opc = SPDK_NVME_OPC_RESERVATION_ACQUIRE;
	bdev_ctx->cmd.cdw10_bits.resv_acquire.racqa = opc;
	bdev_ctx->cmd.cdw10_bits.resv_acquire.iekey = false;
	bdev_ctx->cmd.cdw10_bits.resv_acquire.rtype = SPDK_NVME_RESERVE_WRITE_EXCLUSIVE;

	rc = spdk_bdev_nvme_io_passthru(bdev_ctx->bdev_desc, bdev_ctx->bdev_io_channel,
					&bdev_ctx->cmd, &bdev_ctx->cdata, sizeof(bdev_ctx->cdata),
					reservation_request_cb_fn, NULL);
	if (rc) {
		exit_initiator("failed to submit NVMe I/O command to bdev", rc);
		return;
	}
}

static void
bdev_reservation_release(void *ctx, enum spdk_nvme_reservation_release_action opc, bool success)
{
	int rc = 0;
	struct bdev_context_t *bdev_ctx = ctx;

	memset(&(bdev_ctx->cmd), 0, sizeof(bdev_ctx->cmd));
	memset(&(bdev_ctx->rdata), 0, sizeof(bdev_ctx->rdata));
	g_bdev_pr_ctx->cmd_success_expected = success;

	bdev_ctx->rdata.crkey = bdev_ctx->resv_key;
	bdev_ctx->cmd.opc = SPDK_NVME_OPC_RESERVATION_RELEASE;
	bdev_ctx->cmd.cdw10_bits.resv_acquire.racqa = opc;
	bdev_ctx->cmd.cdw10_bits.resv_acquire.iekey = false;
	bdev_ctx->cmd.cdw10_bits.resv_acquire.rtype = SPDK_NVME_RESERVE_WRITE_EXCLUSIVE;

	rc = spdk_bdev_nvme_io_passthru(bdev_ctx->bdev_desc, bdev_ctx->bdev_io_channel,
					&bdev_ctx->cmd, &bdev_ctx->rdata, sizeof(bdev_ctx->rdata),
					reservation_request_cb_fn, NULL);
	if (rc) {
		exit_initiator("failed to submit NVMe I/O command to bdev", rc);
		return;
	}
}

static void
finalizing_bdev_resv(void)
{
	switch (g_bdev_pr_ctx->bdev_resv_state) {
	case IO_FAIL:
		g_bdev_pr_ctx->bdev_resv_state = UNREGISTER_PRI_HOST;
		bdev_reservation_register(g_bdev_pr_ctx->bdev_ctx_pri_host,
					  SPDK_NVME_RESERVE_UNREGISTER_KEY, true);
		break;
	case UNREGISTER_PRI_HOST:
		g_bdev_pr_ctx->bdev_resv_state = RELEASE;
		bdev_reservation_release(g_bdev_pr_ctx->bdev_ctx_sec_host,
					 SPDK_NVME_RESERVE_RELEASE, true);
		break;
	case RELEASE:
		g_bdev_pr_ctx->bdev_resv_state = UNREGISTER_SEC_HOST;
		bdev_reservation_register(g_bdev_pr_ctx->bdev_ctx_sec_host,
					  SPDK_NVME_RESERVE_UNREGISTER_KEY, true);
		break;
	case UNREGISTER_SEC_HOST:
		pr_test_on = false;
		SPDK_NOTICELOG("test_persistent_reservation_multi_host is successful\n");
		exit_initiator("all test completed successfully", 0);
		break;
	default:
		pr_test_on = false;
		exit_initiator("invalid reservation command", -EINVAL);
	}
}

static void
check_pri_host_resv_via_preempt(void)
{
	switch (g_bdev_pr_ctx->bdev_resv_state) {
	case IO_FAIL:
		g_bdev_pr_ctx->bdev_resv_state = PREEMPT_ACQUIRE;
		bdev_reservation_acquire(g_bdev_pr_ctx->bdev_ctx_pri_host,
					 SPDK_NVME_RESERVE_PREEMPT, true);
		break;
	case PREEMPT_ACQUIRE:
		g_bdev_pr_ctx->bdev_resv_state = IO_SUCCESS;
		test_bdev_write(g_bdev_pr_ctx->bdev_ctx_pri_host, true);
		break;
	case IO_SUCCESS:
		g_bdev_pr_ctx->bdev_resv_state = REGISTER;
		g_bdev_pr_ctx->multi_host_pr_stage = SEC_HOST_RESV_AFTER_PREEMPT;
		bdev_reservation_register(g_bdev_pr_ctx->bdev_ctx_sec_host,
					  SPDK_NVME_RESERVE_REGISTER_KEY, true);
		break;
	default:
		pr_test_on = false;
		exit_initiator("invalid reservation command", -EINVAL);
	}
}

static void
check_sec_host_resv_after_pri_host(void)
{
	switch (g_bdev_pr_ctx->bdev_resv_state) {
	case REGISTER:
		g_bdev_pr_ctx->bdev_resv_state = ACQUIRE_FAIL;
		bdev_reservation_acquire(g_bdev_pr_ctx->bdev_ctx_sec_host,
					 SPDK_NVME_RESERVE_ACQUIRE, false);
		break;
	case ACQUIRE_FAIL:
		g_bdev_pr_ctx->bdev_resv_state = IO_FAIL;
		test_bdev_write(g_bdev_pr_ctx->bdev_ctx_sec_host, false);
		break;
	case IO_FAIL:
		g_bdev_pr_ctx->bdev_resv_state = RELEASE;
		bdev_reservation_release(g_bdev_pr_ctx->bdev_ctx_pri_host,
					 SPDK_NVME_RESERVE_RELEASE, true);
		break;
	case RELEASE:
		g_bdev_pr_ctx->bdev_resv_state = IO_SUCCESS;
		test_bdev_write(g_bdev_pr_ctx->bdev_ctx_sec_host, true);
		break;
	case IO_SUCCESS:
		g_bdev_pr_ctx->bdev_resv_state = ACQUIRE_SUCCESS;
		bdev_reservation_acquire(g_bdev_pr_ctx->bdev_ctx_sec_host,
					 SPDK_NVME_RESERVE_ACQUIRE, true);
		break;
	case ACQUIRE_SUCCESS:
		g_bdev_pr_ctx->bdev_resv_state = IO_FAIL;
		if (g_bdev_pr_ctx->multi_host_pr_stage == SEC_HOST_RESV) {
			g_bdev_pr_ctx->multi_host_pr_stage = PRI_HOST_RESV_PREEMPT;
		} else {
			g_bdev_pr_ctx->multi_host_pr_stage = FINALIZE;
		}
		test_bdev_write(g_bdev_pr_ctx->bdev_ctx_pri_host, false);
		break;
	default:
		pr_test_on = false;
		exit_initiator("invalid reservation command", -EINVAL);
	}
}

static void
check_pri_host_resv(void)
{
	switch (g_bdev_pr_ctx->bdev_resv_state) {
	case REGISTER:
		g_bdev_pr_ctx->bdev_resv_state = ACQUIRE_SUCCESS;
		bdev_reservation_acquire(g_bdev_pr_ctx->bdev_ctx_pri_host,
					 SPDK_NVME_RESERVE_ACQUIRE, true);
		break;
	case ACQUIRE_SUCCESS:
		g_bdev_pr_ctx->bdev_resv_state = IO_SUCCESS;
		test_bdev_write(g_bdev_pr_ctx->bdev_ctx_pri_host, true);
		break;
	case IO_SUCCESS:
		g_bdev_pr_ctx->bdev_resv_state = REGISTER;
		g_bdev_pr_ctx->multi_host_pr_stage = SEC_HOST_RESV;
		bdev_reservation_register(g_bdev_pr_ctx->bdev_ctx_sec_host,
					  SPDK_NVME_RESERVE_REGISTER_KEY, true);
		break;
	default:
		pr_test_on = false;
		exit_initiator("invalid reservation command", -EINVAL);
	}
}

static void
test_pr_multi_host_flow(void)
{
	switch (g_bdev_pr_ctx->multi_host_pr_stage) {
	case PRI_HOST_RESV:
		check_pri_host_resv();
		break;
	case SEC_HOST_RESV:
		check_sec_host_resv_after_pri_host();
		break;
	case PRI_HOST_RESV_PREEMPT:
		check_pri_host_resv_via_preempt();
		break;
	case SEC_HOST_RESV_AFTER_PREEMPT:
		check_sec_host_resv_after_pri_host();
		break;
	case FINALIZE:
		finalizing_bdev_resv();
		break;
	}
}

static void
connect_sec_host_cb(void *ctx, size_t bdev_count, int rc)
{
	uint32_t buf_align;
	struct spdk_bdev *bdev = NULL;
	struct bdev_context_t *bdev_ctx;

	if (rc) {
		exit_initiator("failed to create the bdev", rc);
		return;
	}

	bdev_ctx = g_bdev_pr_ctx->bdev_ctx_sec_host;
	bdev = spdk_bdev_get_by_name(g_bdev_pr_ctx->bdev_names[0]);
	if (bdev == NULL) {
		exit_initiator("could not find the bdev", -ENODEV);
		return;
	}
	bdev_ctx->bdev = bdev;
	bdev_ctx->bdev_name = bdev->name;

	SPDK_NOTICELOG("opening the bdev from sec host %s\n", bdev->name);
	rc = spdk_bdev_open_ext(bdev->name, true, nvmf_bdev_event_cb, NULL,
				&bdev_ctx->bdev_desc);
	if (rc) {
		exit_initiator("could not open bdev", rc);
		return;
	}

	/* Open I/O channel */
	bdev_ctx->bdev_io_channel = spdk_bdev_get_io_channel(bdev_ctx->bdev_desc);
	if (!bdev_ctx->bdev_io_channel) {
		exit_initiator("could not create bdev I/O channel!", -EIO);
		return;
	}

	/* Allocate memory for the io buffer */
	bdev_ctx->buff_size = spdk_bdev_get_block_size(bdev_ctx->bdev) *
			      spdk_bdev_get_write_unit_size(bdev_ctx->bdev);
	buf_align = spdk_bdev_get_buf_align(bdev_ctx->bdev);
	bdev_ctx->buff = spdk_dma_zmalloc(bdev_ctx->buff_size, buf_align, NULL);
	if (!bdev_ctx->buff) {
		exit_initiator("failed to allocate buffer", -ENOMEM);
		return;
	}

	g_bdev_pr_ctx->bdev_ctx_sec_host->resv_key = KEY_2;
	g_bdev_pr_ctx->host_type = MULTI_HOST;
	g_bdev_pr_ctx->multi_host_pr_stage = PRI_HOST_RESV;
	g_bdev_pr_ctx->bdev_resv_state = REGISTER;
	bdev_reservation_register(g_bdev_pr_ctx->bdev_ctx_pri_host,
				  SPDK_NVME_RESERVE_REGISTER_KEY, true);
}

static void
test_persistent_reservation_multi_host(void)
{
	int rc;
	struct bdev_context_t *bdev_ctx;
	struct spdk_nvme_ctrlr_opts ctrlr_opts;
	struct spdk_bdev_nvme_ctrlr_opts bdev_opts = {};

	bdev_ctx = calloc(1, sizeof(*bdev_ctx));
	if (!bdev_ctx) {
		exit_initiator("unable to allocate memory", -ENOMEM);
		return;
	}

	g_bdev_pr_ctx->bdev_ctx_sec_host = bdev_ctx;
	spdk_nvme_ctrlr_get_default_ctrlr_opts(&ctrlr_opts, sizeof(ctrlr_opts));
	bdev_nvme_update_subsystem_nqn(g_bdev_context->bdev);
	memcpy(ctrlr_opts.extended_host_id, SEC_EXT_HOST_ID, sizeof(ctrlr_opts.extended_host_id));
	if (g_sec_hostnqn[0] != '\0') {
		memcpy(ctrlr_opts.hostnqn, g_sec_hostnqn, sizeof(ctrlr_opts.hostnqn));
	}

	rc = spdk_bdev_nvme_create(&g_trid, "SEC_HOST", g_bdev_pr_ctx->bdev_names, 1,
				   connect_sec_host_cb, NULL, &ctrlr_opts, &bdev_opts);
	if (rc) {
		exit_initiator("failed to crreate the bdev by secondary host", rc);
		return;
	}
}

static void
test_pr_single_host_flow(void)
{
	switch (g_bdev_pr_ctx->bdev_resv_state) {
	case REGISTER:
		g_bdev_pr_ctx->bdev_resv_state = ACQUIRE_SUCCESS;
		bdev_reservation_acquire(g_bdev_pr_ctx->bdev_ctx_pri_host,
					 SPDK_NVME_RESERVE_ACQUIRE, true);
		break;
	case ACQUIRE_SUCCESS:
		g_bdev_pr_ctx->bdev_resv_state = IO_SUCCESS;
		test_bdev_write(g_bdev_pr_ctx->bdev_ctx_pri_host, true);
		break;
	case IO_SUCCESS:
		g_bdev_pr_ctx->bdev_resv_state = RELEASE;
		bdev_reservation_release(g_bdev_pr_ctx->bdev_ctx_pri_host,
					 SPDK_NVME_RESERVE_RELEASE, true);
		break;
	case RELEASE:
		g_bdev_pr_ctx->bdev_resv_state = UNREGISTER_PRI_HOST;
		bdev_reservation_register(g_bdev_pr_ctx->bdev_ctx_pri_host,
					  SPDK_NVME_RESERVE_UNREGISTER_KEY, true);
		break;
	case UNREGISTER_PRI_HOST:
		SPDK_NOTICELOG("test_persistent_reservation_single_host is successful\n");
		test_persistent_reservation_multi_host();
		break;
	default:
		pr_test_on = false;
		exit_initiator("invalid reservation command", -EINVAL);
	}
}

static void
reservation_request_cb_fn(struct spdk_bdev_io *bdev_io, bool success, void *ctx)
{
	if (g_bdev_pr_ctx->bdev_resv_state != IO_FAIL && g_bdev_pr_ctx->bdev_resv_state != IO_SUCCESS) {
		spdk_bdev_free_io(bdev_io);
		if (success != g_bdev_pr_ctx->cmd_success_expected) {
			exit_initiator("expected bdev reservation request failed", -1);
			return;
		}
	}

	switch (g_bdev_pr_ctx->host_type) {
	case SINGLE_HOST:
		test_pr_single_host_flow();
		break;
	case MULTI_HOST:
		test_pr_multi_host_flow();
		break;
	}
}

static void
test_persistent_reservation_single_host(void)
{
	pr_test_on = true;
	g_bdev_pr_ctx = calloc(1, sizeof(struct bdev_resv_ctx));
	if (!g_bdev_pr_ctx) {
		exit_initiator("unable to allocate memory", -ENOMEM);
		return;
	}

	g_bdev_context->resv_key = KEY_1;
	g_bdev_pr_ctx->bdev_ctx_pri_host = g_bdev_context;
	g_bdev_pr_ctx->bdev_resv_state = REGISTER;
	g_bdev_pr_ctx->host_type = SINGLE_HOST;
	bdev_reservation_register(g_bdev_pr_ctx->bdev_ctx_pri_host,
				  SPDK_NVME_RESERVE_REGISTER_KEY, true);
}

static void
bdev_read_cb_fn(struct spdk_bdev_io *bdev_io, bool success, void *arg)
{
	struct bdev_context_t *bdev_ctx;

	spdk_bdev_free_io(bdev_io);
	bdev_ctx = arg;

	if (success) {
		if (strcmp(bdev_ctx->buff, "Hello World!")) {
			exit_initiator("string don't match", -EIO);
			return;
		}
	} else {
		exit_initiator("bdev read operation failed", -EIO);
		return;
	}
	SPDK_NOTICELOG("expected bdev read operation is successful\n");

	/* moving on to next testing state */
	if (pr_test_on) {
		reservation_request_cb_fn(bdev_io, success, NULL);
	} else {
		SPDK_NOTICELOG("test_io_operations is successful\n");
		test_persistent_reservation_single_host();
	}
}

static void
test_bdev_read(void *ctx)
{
	int rc;
	struct bdev_context_t *bdev_ctx;

	bdev_ctx = ctx;
	/* Zero the buffer so that we can use it for reading */
	memset(bdev_ctx->buff, 0, bdev_ctx->buff_size);

	SPDK_NOTICELOG("reading from bdev\n");
	rc = spdk_bdev_read(bdev_ctx->bdev_desc, bdev_ctx->bdev_io_channel,
			    bdev_ctx->buff, 0, bdev_ctx->buff_size, bdev_read_cb_fn,
			    bdev_ctx);
	if (rc) {
		exit_initiator("error while reading from bdev", rc);
		return;
	}
}

static void
bdev_write_cb_fn(struct spdk_bdev_io *bdev_io, bool success, void *arg)
{
	/* Complete the I/O */
	spdk_bdev_free_io(bdev_io);

	if (success != io_success_expected) {
		exit_initiator("expected bdev write operation failed", -EIO);
		return;
	}
	SPDK_NOTICELOG("expected bdev write operation is successful\n");

	test_bdev_read(arg);
}

static void
test_bdev_write(void *ctx, bool arg)
{
	int rc;
	struct bdev_context_t *bdev_ctx;

	bdev_ctx = ctx;
	io_success_expected = arg;
	snprintf(bdev_ctx->buff, bdev_ctx->buff_size, "%s", "Hello World!");

	SPDK_NOTICELOG("writing to the bdev\n");
	rc = spdk_bdev_write(bdev_ctx->bdev_desc, bdev_ctx->bdev_io_channel,
			     bdev_ctx->buff, 0, bdev_ctx->buff_size, bdev_write_cb_fn,
			     bdev_ctx);
	if (rc) {
		exit_initiator("error while writing to bdev", rc);
		return;
	}
}

static void
test_io_operations(void)
{
	uint32_t buf_align;

	/* Allocate memory for the io buffer */
	g_bdev_context->buff_size = spdk_bdev_get_block_size(g_bdev_context->bdev) *
				    spdk_bdev_get_write_unit_size(g_bdev_context->bdev);
	buf_align = spdk_bdev_get_buf_align(g_bdev_context->bdev);
	g_bdev_context->buff = spdk_dma_zmalloc(g_bdev_context->buff_size, buf_align, NULL);
	if (!g_bdev_context->buff) {
		exit_initiator("failed to allocate buffer", -ENOMEM);
		return;
	}

	test_bdev_write(g_bdev_context, true);
}

static void
discovery_and_connect_cb_fn(void *arg, int rc)
{
	struct spdk_bdev *bdev;

	if (rc) {
		exit_initiator("failed to discover the bdev", rc);
		return;
	}

	bdev = spdk_bdev_first_leaf();
	if (bdev == NULL) {
		exit_initiator("could not find the bdev", -ENODEV);
		return;
	}
	g_bdev_context->bdev = bdev;
	g_bdev_context->bdev_name = bdev->name;

	SPDK_NOTICELOG("opening the bdev %s\n", g_bdev_context->bdev_name);
	rc = spdk_bdev_open_ext(g_bdev_context->bdev_name, true, nvmf_bdev_event_cb, NULL,
				&g_bdev_context->bdev_desc);
	if (rc) {
		exit_initiator("could not open bdev", rc);
		return;
	}

	/* Open I/O channel */
	g_bdev_context->bdev_io_channel = spdk_bdev_get_io_channel(g_bdev_context->bdev_desc);
	if (g_bdev_context->bdev_io_channel == NULL) {
		exit_initiator("could not create bdev I/O channel!", -EIO);
		return;
	}
	SPDK_NOTICELOG("test_discovery_and_connect is successful\n");

	/* moving on to next testing state */
	test_io_operations();
}

static void
test_discovery_and_connect(void)
{
	int rc;
	struct spdk_nvme_ctrlr_opts ctrlr_opts;
	struct spdk_bdev_nvme_opts opts;
	struct spdk_bdev_nvme_ctrlr_opts bdev_opts = {};

	spdk_nvme_ctrlr_get_default_ctrlr_opts(&ctrlr_opts, sizeof(ctrlr_opts));
	if (g_pri_hostnqn[0] != '\0') {
		memcpy(ctrlr_opts.hostnqn, g_pri_hostnqn, sizeof(ctrlr_opts.hostnqn));
	}
	memcpy(ctrlr_opts.extended_host_id, EXT_HOST_ID, sizeof(ctrlr_opts.extended_host_id));

	/* setting below flag to enable registering two bdevs from two hosts in single spdk env */
	spdk_bdev_nvme_get_opts(&opts, sizeof(opts));
	opts.generate_random_uuids = true;
	spdk_bdev_nvme_set_opts(&opts);

	rc = bdev_nvme_start_discovery(&g_trid, "PRI_HOST", &ctrlr_opts, &bdev_opts,
				       0, false, discovery_and_connect_cb_fn, NULL);

	if (rc) {
		exit_initiator("failed to start bdev discovery", rc);
		return;
	}
}

int
main(int argc, char **argv)
{
	int rc;

	g_bdev_context = calloc(1, sizeof(*g_bdev_context));
	if (!g_bdev_context) {
		SPDK_ERRLOG("unable to allocate memory\n");
		return -ENOMEM;
	}

	g_env_opts.opts_size = sizeof(g_env_opts);
	spdk_env_opts_init(&g_env_opts);
	g_env_opts.name = "nvmf_bdev_initiator";

	rc = parse_args(argc, argv);
	if (rc != 0) {
		return rc;
	}

	if (spdk_env_init(&g_env_opts) != 0) {
		SPDK_ERRLOG("unable to initialize SPDK env\n");
		return -1;
	}
	spdk_thread_lib_init(NULL, 0);

	g_app_thread = spdk_thread_create("bdev_initiator", NULL);
	if (!g_app_thread) {
		SPDK_ERRLOG("failed to create thread\n");
		spdk_env_fini();
		return -1;
	}
	spdk_set_thread(g_app_thread);
	spdk_subsystem_init(subsystem_init_done, NULL);

	/* app flow :
	 * test_discovery_and_connect -> discovery_and_connect_cb_fn -> test_io_operations
	 * test_io_operations -> bdev_read_cb_fn -> test_persistent_reservation_single_host
	 * test_persistent_reservation_single_host -> ...
	 */
	test_discovery_and_connect();

	while (spdk_thread_is_running(g_app_thread)) {
		spdk_thread_poll(g_app_thread, 0, 0);
	}

	spdk_env_fini();
	free(g_bdev_context);
	return 0;
}
