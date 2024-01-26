/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright 2023 Solidigm All Rights Reserved
 *   All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk/thread.h"
#include "spdk/ftl.h"

#include "ftl_core.h"
#include "utils/ftl_log.h"
#include "mngt/ftl_mngt_steps.h"

#define FTL_ADMIN_POLLER_PERIOD_US (1000UL * 50) /* 50 ms */

struct ftl_admin_poller_ctx {
	/* Admin poller */
	struct spdk_poller		*poller;
};

static int
ftl_admin_poller(void *pctx)
{
	return SPDK_POLLER_IDLE;
}

void
ftl_mngt_start_admin_poller(struct spdk_ftl_dev *dev, struct ftl_mngt_process *mngt)
{
	struct ftl_admin_poller_ctx *ctx = calloc(1, sizeof(*ctx));

	assert(NULL == dev->admin_poller_ctx);
	if (!ctx) {
		FTL_ERRLOG(dev, "Unable to allocate memory for admin poller context\n");
		ftl_mngt_fail_step(mngt);
		return;
	}

	ctx->poller = SPDK_POLLER_REGISTER(ftl_admin_poller, dev, FTL_ADMIN_POLLER_PERIOD_US);
	if (!ctx->poller) {
		free(ctx);
		FTL_ERRLOG(dev, "Unable to register admin poller\n");
		ftl_mngt_fail_step(mngt);
		return;
	}

	dev->admin_poller_ctx = ctx;
	ftl_mngt_next_step(mngt);
}

void
ftl_mngt_stop_admin_poller(struct spdk_ftl_dev *dev, struct ftl_mngt_process *mngt)
{
	struct ftl_admin_poller_ctx *ctx = dev->admin_poller_ctx;

	if (ctx) {
		spdk_poller_unregister(&ctx->poller);
		free(ctx);
		dev->admin_poller_ctx = NULL;
	}

	ftl_mngt_next_step(mngt);
}
