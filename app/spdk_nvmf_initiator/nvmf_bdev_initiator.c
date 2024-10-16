/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Nutanix Inc. All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/event.h"
#include "module/bdev/nvme/bdev_nvme.h"


static struct spdk_nvme_transport_id g_trid;
static struct spdk_thread *g_app_thread;
static char g_hostnqn[SPDK_NVMF_NQN_MAX_LEN + 1];
static bool test_failed = false;	/* To capture any error occured in callback functions */
static int g_dpdk_mem = 0;
static bool g_no_huge = false;
static int outstanding_commands;
#define CHECK_TEST_STATUS(rc, msg) \
	do { \
		if (rc) { \
			SPDK_ERRLOG("%s failed\n", msg); \
			return -1; \
		} else { \
			SPDK_NOTICELOG("%s is successful\n", msg); \
		} \
	} while (0)

struct bdev_context_t {
	struct spdk_bdev *bdev;
	struct spdk_bdev_desc *bdev_desc;
	struct spdk_io_channel *bdev_io_channel;
	struct spdk_nvme_cmd cmd;
	char *buff;
	uint32_t buff_size;
	char *bdev_name;
	uint8_t host_id;
};
struct callback_arg {
	struct bdev_context_t *bdev_context;
	bool success_expected;
};

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
	printf(" -d         DPDK huge memory size in MB\n");
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

	while ((op = getopt(argc, argv, "nd:p:r:H")) != -1) {
		switch (op) {
		case 'n':
			g_no_huge = true;
			break;
		case 'd':
			g_dpdk_mem = spdk_strtol(optarg, 10);
			if (g_dpdk_mem < 0) {
				fprintf(stderr, "invalid DPDK memory size\n");
				return g_dpdk_mem;
			}
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
				if (len > (sizeof(g_hostnqn) - 1)) {
					fprintf(stderr, "host NQN is too long\n");
					return 1;
				}
				memcpy(g_hostnqn, hostnqn, len);
				g_hostnqn[len] = '\0';
			}
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

static int
test_multipathing(void *ctx)
{
	/* TODO : implement multipathing in this function
	 */
	return -1;
}

static int
test_persistent_reservation_multi_host(void *ctx)
{
	/* TODO : implement persistent reservation tests from multi host
	 */
	return -1;
}

static int
test_persistent_reservation_single_host(void *ctx)
{
	/* TODO : implement persistent reservation tests in this function
	 */
	return -1;
}

static void
bdev_read_cb_fn(struct spdk_bdev_io *bdev_io, bool success, void *ctx)
{
	struct callback_arg *io_cb_arg = ctx;
	struct bdev_context_t *bdev_context = io_cb_arg->bdev_context;

	if (success) {
		SPDK_NOTICELOG("read string from bdev : %s\n", bdev_context->buff);
		if (strcmp(bdev_context->buff, "Hello World!")) {
			SPDK_ERRLOG("read string different from the written string\n");
			success = false;
		}
	}
	if (!success) {
		SPDK_ERRLOG("bdev io read error\n");
	}

	test_failed = (success != io_cb_arg->success_expected);
	spdk_bdev_free_io(bdev_io);
	outstanding_commands--;
}

static int
test_bdev_read(void *ctx)
{
	struct callback_arg *io_cb_arg = ctx;
	struct bdev_context_t *bdev_context = io_cb_arg->bdev_context;
	int rc = 0;

	SPDK_NOTICELOG("reading from bdev\n");
	rc = spdk_bdev_read(bdev_context->bdev_desc, bdev_context->bdev_io_channel,
			    bdev_context->buff, 0, bdev_context->buff_size, bdev_read_cb_fn,
			    io_cb_arg);
	if (rc) {
		SPDK_ERRLOG("%s error while reading from bdev: %d\n", spdk_strerror(-rc), rc);
		return rc;
	}
	outstanding_commands++;

	/* wait for the callback funcion */
	while (outstanding_commands) {
		spdk_thread_poll(g_app_thread, 0, 0);
	}

	if (test_failed) {
		rc = -1;
	}
	return rc;
}

static void
bdev_write_cb_fn(struct spdk_bdev_io *bdev_io, bool success, void *ctx)
{
	/* Complete the I/O */
	spdk_bdev_free_io(bdev_io);
	struct callback_arg *io_cb_arg = ctx;

	if (success) {
		SPDK_NOTICELOG("bdev io write completed successfully\n");
	} else {
		SPDK_ERRLOG("bdev io write error: %d\n", EIO);
	}

	test_failed = (success != io_cb_arg->success_expected);
	outstanding_commands--;
}

static int
test_bdev_write(void *ctx)
{
	struct callback_arg *io_cb_arg = ctx;
	struct bdev_context_t *bdev_context = io_cb_arg->bdev_context;
	int rc = 0;

	snprintf(bdev_context->buff, bdev_context->buff_size, "%s", "Hello World!");

	SPDK_NOTICELOG("writing to the bdev\n");
	rc = spdk_bdev_write(bdev_context->bdev_desc, bdev_context->bdev_io_channel,
			     bdev_context->buff, 0, bdev_context->buff_size, bdev_write_cb_fn,
			     io_cb_arg);
	if (rc) {
		SPDK_ERRLOG("%s error while writing to bdev: %d\n", spdk_strerror(-rc), rc);
		return rc;
	}
	outstanding_commands++;

	/* wait for callback function to complete */
	while (outstanding_commands) {
		spdk_thread_poll(g_app_thread, 0, 0);
	}

	if (test_failed) {
		rc = -1;
	}
	return rc;
}

static int
test_io_operations(void *ctx, bool success_expected)
{
	struct bdev_context_t *bdev_context = ctx;
	struct callback_arg *io_cb_arg = calloc(1, sizeof(*io_cb_arg));
	io_cb_arg->bdev_context = ctx;
	io_cb_arg->success_expected = success_expected;
	uint32_t buf_align;
	int rc = 0;

	/* Allocate memory for the io buffer */
	if (!bdev_context->buff) {
		bdev_context->buff_size = spdk_bdev_get_block_size(bdev_context->bdev) *
					  spdk_bdev_get_write_unit_size(bdev_context->bdev);
		buf_align = spdk_bdev_get_buf_align(bdev_context->bdev);
		bdev_context->buff = spdk_dma_zmalloc(bdev_context->buff_size, buf_align, NULL);
		if (!bdev_context->buff) {
			SPDK_ERRLOG("failed to allocate buffer\n");
			return -1;
		}
	}

	rc = test_bdev_write(io_cb_arg);
	if (rc) {
		SPDK_ERRLOG("expected write operation failed\n");
		return rc;
	}

	/* Zero the buffer so that we can use it for reading */
	memset(bdev_context->buff, 0, bdev_context->buff_size);

	rc = test_bdev_read(io_cb_arg);
	if (rc) {
		SPDK_ERRLOG("expected read operation failed\n");
		return rc;
	}

	free(io_cb_arg);
	return 0;
}

static void
discovery_and_connect_cb_fn(void *ctx, int rc)
{
	if (rc) {
		SPDK_ERRLOG("failed to get the bdev\n");
		goto end;
	}
	struct bdev_context_t *bdev_context = ctx;
	struct spdk_bdev *bdev;

	bdev = spdk_bdev_first_leaf();
	if (bdev == NULL) {
		SPDK_ERRLOG("could not find the bdev\n");
		goto end;
	}
	bdev_context->bdev = bdev;
	bdev_context->bdev_name = bdev->name;

	SPDK_NOTICELOG("opening the bdev %s\n", bdev_context->bdev_name);
	rc = spdk_bdev_open_ext(bdev_context->bdev_name, true, nvmf_bdev_event_cb, NULL,
				&bdev_context->bdev_desc);
	if (rc) {
		SPDK_ERRLOG("could not open bdev: %s\n", bdev_context->bdev_name);
		goto end;
	}

	/* Open I/O channel */
	bdev_context->bdev_io_channel = spdk_bdev_get_io_channel(bdev_context->bdev_desc);
	if (bdev_context->bdev_io_channel == NULL) {
		SPDK_ERRLOG("could not create bdev I/O channel!\n");
		goto end;
	}

	outstanding_commands--;
	return;

end:
	test_failed = true;
	outstanding_commands--;
}

static int
test_discovery_and_connect(void *ctx)
{
	int rc;
	struct bdev_context_t *bdev_context = ctx;
	struct spdk_nvme_ctrlr_opts ctrlr_opts;
	struct spdk_bdev_nvme_ctrlr_opts bdev_opts = {};
	outstanding_commands = 0;

	spdk_nvme_ctrlr_get_default_ctrlr_opts(&ctrlr_opts, sizeof(ctrlr_opts));
	if (g_hostnqn[0] != '\0') {
		memcpy(ctrlr_opts.hostnqn, g_hostnqn, sizeof(ctrlr_opts.hostnqn));
	}

	rc = bdev_nvme_start_discovery(&g_trid, ctrlr_opts.hostnqn, &ctrlr_opts, &bdev_opts,
				       0, false, discovery_and_connect_cb_fn, bdev_context);

	if (rc) {
		SPDK_ERRLOG("test_discovery_and_connect failed to start\n");
		return rc;
	}
	/* atomic updation is not required as there is only one thread accessing it */
	outstanding_commands++;

	/* wait for callback function to complete */
	while (outstanding_commands) {
		spdk_thread_poll(g_app_thread, 0, 0);
	}

	if (test_failed) {
		rc = -1;
	}
	return rc;
}

static int
test_bdev_initiator(void *ctx)
{
	spdk_subsystem_init(subsystem_init_done, NULL);
	CHECK_TEST_STATUS(test_discovery_and_connect(ctx), "test_discovery_and_connect");
	CHECK_TEST_STATUS(test_io_operations(ctx, true), "test_io_operations");
	CHECK_TEST_STATUS(test_persistent_reservation_single_host(ctx), "test_single_host_pr");
	CHECK_TEST_STATUS(test_persistent_reservation_multi_host(ctx), "test_multi_host_pr");
	CHECK_TEST_STATUS(test_multipathing(ctx), "test_multipathing");

	return 0;
}

int
main(int argc, char **argv)
{
	int rc;
	rc = parse_args(argc, argv);
	if (rc != 0) {
		return rc;
	}

	struct bdev_context_t *bdev_context = calloc(1, sizeof(*bdev_context));
	if (!bdev_context) {
		SPDK_ERRLOG("unable to allocate memory\n");
		return -ENOMEM;
	}

	struct spdk_env_opts *env_opts = calloc(1, sizeof(*env_opts));
	env_opts->opts_size = sizeof(*env_opts);
	spdk_env_opts_init(env_opts);
	env_opts->name = "nvmf_bdev_initiator";
	env_opts->no_huge = g_no_huge;
	env_opts->mem_size = g_dpdk_mem;
	env_opts->no_pci = true;

	if (spdk_env_init(env_opts) != 0) {
		fprintf(stderr, "Unable to initialize SPDK env\n");
		return 1;
	}
	spdk_thread_lib_init(NULL, 0);

	g_app_thread = spdk_thread_create("bdev_initiator", NULL);
	if (!g_app_thread) {
		SPDK_ERRLOG("failed to create thread\n");
		return -1;
	}
	spdk_set_thread(g_app_thread);

	rc = test_bdev_initiator(bdev_context);
	if (rc == 0) {
		SPDK_NOTICELOG("nvmf_bdev_initiator test is successful\n");
	} else {
		SPDK_ERRLOG("nvmf_bdev_initiator test failed\n");
	}

	spdk_env_fini();
	free(bdev_context);
	return 0;
}
