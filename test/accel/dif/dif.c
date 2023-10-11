/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2023 Intel Corporation.
 *   All rights reserved.
 */

#include "spdk/stdinc.h"

#include "spdk/accel.h"
#include "spdk/env.h"
#include "spdk/log.h"
#include "spdk/thread.h"
#include "spdk/event.h"
#include "spdk/rpc.h"
#include "spdk/util.h"
#include "spdk/string.h"

#include "CUnit/Basic.h"

pthread_mutex_t g_test_mutex;
pthread_cond_t g_test_cond;

static struct spdk_thread *g_thread_ut;
static struct spdk_thread *g_thread_io;
static int g_num_failures = 0;
static bool g_shutdown = false;
static bool g_completion_success;
struct spdk_io_channel	*g_channel = NULL;

struct dif_task {
	struct iovec		*src_iovs;
	uint32_t		src_iovcnt;
	uint32_t		num_blocks; /* used for the DIF related operations */
	struct spdk_dif_ctx	dif_ctx;
	struct spdk_dif_error	dif_error;
};

static void
execute_spdk_function(spdk_msg_fn fn, void *arg)
{
	pthread_mutex_lock(&g_test_mutex);
	spdk_thread_send_msg(g_thread_io, fn, arg);
	pthread_cond_wait(&g_test_cond, &g_test_mutex);
	pthread_mutex_unlock(&g_test_mutex);
}

static void
wake_ut_thread(void)
{
	pthread_mutex_lock(&g_test_mutex);
	pthread_cond_signal(&g_test_cond);
	pthread_mutex_unlock(&g_test_mutex);
}

static void
exit_io_thread(void *arg)
{
	assert(spdk_get_thread() == g_thread_io);
	spdk_thread_exit(g_thread_io);
	wake_ut_thread();
}

#define DATA_PATTERN 0x5A

static int g_xfer_size_bytes = 4096;
static int g_block_size_bytes = 512;
static int g_md_size_bytes = 8;
static uint32_t g_chained_count = 1;
struct dif_task g_dif_task;

struct accel_dif_request {
	struct spdk_io_channel *channel;
	struct iovec *src_iovs;
	size_t src_iovcnt;
	uint32_t num_blocks;
	const struct spdk_dif_ctx *ctx;
	struct spdk_dif_error *error;
	spdk_accel_completion_cb cb_fn;
	void *cb_arg;
};

static void
accel_dif_oper_done(void *arg1, int status)
{
	if (status == 0) {
		g_completion_success = true;
	}
	wake_ut_thread();
}

static int
get_dif_verify_alloc_bufs(struct dif_task *task)
{
	int src_buff_len = g_xfer_size_bytes;
	uint32_t i = 0;

	assert(g_chained_count > 0);
	task->src_iovcnt = g_chained_count;
	task->src_iovs = calloc(task->src_iovcnt, sizeof(struct iovec));
	if (!task->src_iovs) {
		fprintf(stderr, "Cannot allocated src_iovs\n");
		return -ENOMEM;
	}

	src_buff_len += (g_xfer_size_bytes / g_block_size_bytes) * g_md_size_bytes;

	for (i = 0; i < task->src_iovcnt; i++) {
		task->src_iovs[i].iov_base = spdk_dma_zmalloc(src_buff_len, 0, NULL);
		if (task->src_iovs[i].iov_base == NULL) {
			fprintf(stderr, "Cannot allocated src_iovs[i].iov_base\n");
			return -ENOMEM;
		}
		memset(task->src_iovs[i].iov_base, DATA_PATTERN, src_buff_len);
		task->src_iovs[i].iov_len = src_buff_len;
	}

	task->num_blocks  = (g_xfer_size_bytes * g_chained_count) / g_block_size_bytes;

	return 0;
}

static void
accel_dif_verify_test(void *arg)
{
	int rc;
	struct accel_dif_request *req = arg;

	g_completion_success = false;
	rc = spdk_accel_submit_dif_verify(req->channel, req->src_iovs, req->src_iovcnt,
					  req->num_blocks, req->ctx, req->error, req->cb_fn, req->cb_arg);
	if (rc) {
		wake_ut_thread();
	}
}


static void
accel_dif_verify_op_dif_generated_guard_check(void)
{
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	struct accel_dif_request req;
	struct dif_task *task = &g_dif_task;
	int rc;

	rc = get_dif_verify_alloc_bufs(task);
	CU_ASSERT(rc == 0);

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	rc = spdk_dif_ctx_init(&task->dif_ctx,
			       g_block_size_bytes + g_md_size_bytes,
			       g_md_size_bytes, true, true,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK,
			       16, 0xFFFF, 10, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = spdk_dif_generate(task->src_iovs, task->src_iovcnt, task->num_blocks, &task->dif_ctx);
	CU_ASSERT(rc == 0);

	req.channel = g_channel;
	req.src_iovs = task->src_iovs;
	req.src_iovcnt = task->src_iovcnt;
	req.num_blocks = task->num_blocks;
	req.ctx = &task->dif_ctx;
	req.error = &task->dif_error;
	req.cb_fn = accel_dif_oper_done;
	req.cb_arg = task;

	execute_spdk_function(accel_dif_verify_test, &req);
	CU_ASSERT_EQUAL(g_completion_success, true);
}

static void
dif_verify_op_dif_not_generated_guard_check(void)
{
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	struct accel_dif_request req;
	struct dif_task *task = &g_dif_task;
	int rc;

	rc = get_dif_verify_alloc_bufs(task);
	CU_ASSERT(rc == 0);

	rc = spdk_dif_ctx_init(&task->dif_ctx,
			       g_block_size_bytes + g_md_size_bytes,
			       g_md_size_bytes, true, true,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK,
			       16, 0xFFFF, 20, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	req.channel = g_channel;
	req.src_iovs = task->src_iovs;
	req.src_iovcnt = task->src_iovcnt;
	req.num_blocks = task->num_blocks;
	req.ctx = &task->dif_ctx;
	req.error = &task->dif_error;
	req.cb_fn = accel_dif_oper_done;
	req.cb_arg = task;

	execute_spdk_function(accel_dif_verify_test, &req);
	CU_ASSERT_EQUAL(g_completion_success, false);
}

static void
_stop_init_thread(void *arg)
{
	unsigned num_failures = g_num_failures;

	g_num_failures = 0;

	assert(spdk_get_thread() == g_thread_ut);
	assert(spdk_thread_is_app_thread(NULL));
	execute_spdk_function(exit_io_thread, NULL);
	spdk_app_stop(num_failures);
}

static void
stop_init_thread(unsigned num_failures, struct spdk_jsonrpc_request *request)
{
	g_num_failures = num_failures;

	spdk_thread_send_msg(g_thread_ut, _stop_init_thread, request);
}

static int
suite_init(void)
{
	return 0;
}

static int
suite_fini(void)
{
	return 0;
}

#define SUITE_NAME_MAX 64

static int
setup_accel_tests(void)
{
	unsigned rc = 0;
	CU_pSuite suite = NULL;

	suite = CU_add_suite("accel_dif", suite_init, suite_fini);
	if (suite == NULL) {
		CU_cleanup_registry();
		rc = CU_get_error();
		return -rc;
	}

	if (CU_add_test(suite, "verify: DIF generated, GUARD check",
			accel_dif_verify_op_dif_generated_guard_check) == NULL ||
	    CU_add_test(suite, "verify: DIF not generated, GUARD check",
			dif_verify_op_dif_not_generated_guard_check) == NULL) {
		CU_cleanup_registry();
		rc = CU_get_error();
		return -rc;
	}
	return 0;
}

static void
get_io_channel(void *arg)
{
	g_channel = spdk_accel_get_io_channel();
	assert(g_channel);
	wake_ut_thread();
}

static void
put_io_channel(void *arg)
{
	assert(g_channel);
	spdk_put_io_channel(g_channel);
	wake_ut_thread();
}

static void
run_accel_test_thread(void *arg)
{
	struct spdk_jsonrpc_request *request = arg;
	int rc = 0;

	execute_spdk_function(get_io_channel, NULL);
	if (g_channel == NULL) {
		fprintf(stderr, "Unable to get an accel channel\n");
		goto ret;
	}

	if (CU_initialize_registry() != CUE_SUCCESS) {
		/* CUnit error, probably won't recover */
		rc = CU_get_error();
		rc = -rc;
		goto ret;
	}

	rc = setup_accel_tests();
	if (rc < 0) {
		/* CUnit error, probably won't recover */
		rc = -rc;
		goto ret;
	}
	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	rc = CU_get_number_of_failures();
	CU_cleanup_registry();

ret:
	if (g_channel != NULL) {
		execute_spdk_function(put_io_channel, NULL);
	}
	stop_init_thread(rc, request);
}

static void
accel_dif_test_main(void *arg1)
{
	struct spdk_cpuset tmpmask = {};
	uint32_t i;

	pthread_mutex_init(&g_test_mutex, NULL);
	pthread_cond_init(&g_test_cond, NULL);

	/* This test runs specifically on at least two cores.
	 * g_thread_ut is the app_thread on main core from event framework.
	 * Next one is only for the tests and should always be on separate CPU cores. */
	if (spdk_env_get_core_count() < 3) {
		spdk_app_stop(-1);
		return;
	}

	SPDK_ENV_FOREACH_CORE(i) {
		if (i == spdk_env_get_current_core()) {
			g_thread_ut = spdk_get_thread();
			continue;
		}
		spdk_cpuset_zero(&tmpmask);
		spdk_cpuset_set_cpu(&tmpmask, i, true);
		if (g_thread_io == NULL) {
			g_thread_io = spdk_thread_create("io_thread", &tmpmask);
		}

	}

	spdk_thread_send_msg(g_thread_ut, run_accel_test_thread, NULL);
}

static void
accel_dif_usage(void)
{
}

static int
accel_dif_parse_arg(int ch, char *arg)
{
	return 0;
}

static void
spdk_dif_shutdown_cb(void)
{
	g_shutdown = true;
	spdk_thread_send_msg(g_thread_ut, _stop_init_thread, NULL);
}

int
main(int argc, char **argv)
{
	int			rc;
	struct spdk_app_opts	opts = {};

	spdk_app_opts_init(&opts, sizeof(opts));
	opts.name = "DIF";
	opts.reactor_mask = "0x7";
	opts.shutdown_cb = spdk_dif_shutdown_cb;

	if ((rc = spdk_app_parse_args(argc, argv, &opts, "w", NULL,
				      accel_dif_parse_arg, accel_dif_usage)) !=
	    SPDK_APP_PARSE_ARGS_SUCCESS) {
		return rc;
	}

	rc = spdk_app_start(&opts, accel_dif_test_main, NULL);
	spdk_app_fini();

	return rc;
}
