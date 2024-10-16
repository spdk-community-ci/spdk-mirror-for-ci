/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Nutanix Inc. All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/event.h"
#include "module/bdev/nvme/bdev_nvme.h"

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

static struct spdk_nvme_transport_id g_trid;
static struct spdk_thread *g_app_thread;
static char g_hostnqn[SPDK_NVMF_NQN_MAX_LEN + 1];
static bool io_success_expected;
static struct bdev_context_t *g_bdev_context;
static struct spdk_env_opts g_env_opts;

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

static void
test_multipathing(void)
{
	/* TODO : implement multipathing in this function
	 */

	/* exiting the initiator successfully */
	exit_initiator("all test completed", 0);
	return;
}

static void
test_persistent_reservation_multi_host(void)
{
	/* TODO : implement persistent reservation tests from multi host
	 */

	/* moving on to next testing state */
	test_multipathing();
}

static void
test_persistent_reservation_single_host(void)
{
	/* TODO : implement persistent reservation tests in this function
	 */

	/* moving on to next testing state */
	test_persistent_reservation_multi_host();
}

static void
bdev_read_cb_fn(struct spdk_bdev_io *bdev_io, bool success, void *arg)
{
	spdk_bdev_free_io(bdev_io);

	if (success) {
		if (strcmp(g_bdev_context->buff, "Hello World!")) {
			exit_initiator("string don't match", -EIO);
			return;
		}
	} else {
		exit_initiator("bdev read operation failed", -EIO);
		return;
	}
	SPDK_NOTICELOG("expected bdev read operation is successful\n");

	/* moving on to next testing state */
	test_persistent_reservation_single_host();
}

static void
test_bdev_read(void)
{
	int rc;

	/* Zero the buffer so that we can use it for reading */
	memset(g_bdev_context->buff, 0, g_bdev_context->buff_size);

	SPDK_NOTICELOG("reading from bdev\n");
	rc = spdk_bdev_read(g_bdev_context->bdev_desc, g_bdev_context->bdev_io_channel,
			    g_bdev_context->buff, 0, g_bdev_context->buff_size, bdev_read_cb_fn,
			    NULL);
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

	test_bdev_read();
}

static void
test_bdev_write(void)
{
	int rc;

	snprintf(g_bdev_context->buff, g_bdev_context->buff_size, "%s", "Hello World!");

	SPDK_NOTICELOG("writing to the bdev\n");
	rc = spdk_bdev_write(g_bdev_context->bdev_desc, g_bdev_context->bdev_io_channel,
			     g_bdev_context->buff, 0, g_bdev_context->buff_size, bdev_write_cb_fn,
			     NULL);
	if (rc) {
		exit_initiator("error while writing to bdev", rc);
		return;
	}
}

static void
test_io_operations(bool arg)
{
	uint32_t buf_align;

	io_success_expected = arg;

	/* Allocate memory for the io buffer */
	if (!g_bdev_context->buff) {
		g_bdev_context->buff_size = spdk_bdev_get_block_size(g_bdev_context->bdev) *
					    spdk_bdev_get_write_unit_size(g_bdev_context->bdev);
		buf_align = spdk_bdev_get_buf_align(g_bdev_context->bdev);
		g_bdev_context->buff = spdk_dma_zmalloc(g_bdev_context->buff_size, buf_align, NULL);
		if (!g_bdev_context->buff) {
			exit_initiator("failed to allocate buffer", -ENOMEM);
			return;
		}
	}

	test_bdev_write();
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
	test_io_operations(true);
}

static void
test_discovery_and_connect(void)
{
	int rc;
	struct spdk_nvme_ctrlr_opts ctrlr_opts;
	struct spdk_bdev_nvme_ctrlr_opts bdev_opts = {};

	spdk_nvme_ctrlr_get_default_ctrlr_opts(&ctrlr_opts, sizeof(ctrlr_opts));
	if (g_hostnqn[0] != '\0') {
		memcpy(ctrlr_opts.hostnqn, g_hostnqn, sizeof(ctrlr_opts.hostnqn));
	}

	rc = bdev_nvme_start_discovery(&g_trid, ctrlr_opts.hostnqn, &ctrlr_opts, &bdev_opts,
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
