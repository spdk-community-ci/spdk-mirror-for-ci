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
#define FTL_IDLE_TIME_THRESHOLD_S 60UL /* 60 s */

struct ftl_admin_poller_ctx {
	/* Admin poller */
	struct spdk_poller		*poller;

	/* This flag indicates FTL idle time - no user writes in the given time window */
	bool				idle;

	/* Last IO activity count */
	uint64_t			last_ios;

	uint64_t			last_tsc;

	uint64_t			sum_tsc;

	uint64_t			idle_tsc;
};

static void
admin_task_detect_idle(struct spdk_ftl_dev *dev, struct ftl_admin_poller_ctx *ctx)
{
	uint64_t ios = dev->stats.entries[FTL_STATS_TYPE_USER].read.ios +
		       dev->stats.entries[FTL_STATS_TYPE_USER].write.ios;
	uint64_t current_tsc = spdk_thread_get_last_tsc(spdk_get_thread());

	if (ctx->last_ios != ios) {
		if (ctx->idle) {
			FTL_DEBUGLOG(dev, "Active state detected\n");
		}

		/* User IO presence since last check */
		ctx->last_ios = ios;
		ctx->sum_tsc = 0;
		ctx->idle = false;
		return;
	} else {
		/* No IO since last check */
		if (!ctx->idle) {
			ctx->sum_tsc += current_tsc - ctx->last_tsc;
			if (ctx->sum_tsc > ctx->idle_tsc) {
				FTL_DEBUGLOG(dev, "Idle state detected\n");
				ctx->idle = true;
			}
		}
	}

	ctx->last_tsc = current_tsc;
}

static int
ftl_admin_poller(void *pctx)
{
	struct spdk_ftl_dev *dev = pctx;
	struct ftl_admin_poller_ctx *ctx = dev->admin_poller_ctx;

	admin_task_detect_idle(dev, ctx);
	return SPDK_POLLER_BUSY;
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

	/* Idle time activator */
	ctx->idle_tsc = spdk_get_ticks_hz() * FTL_IDLE_TIME_THRESHOLD_S;
	ctx->last_tsc = spdk_thread_get_last_tsc(spdk_get_thread());

	ctx->poller = SPDK_POLLER_REGISTER(ftl_admin_poller, dev, FTL_ADMIN_POLLER_PERIOD_US);
	if (!ctx->poller) {
		free(ctx);
		FTL_ERRLOG(dev, "Unable to register admin poller\n");
		ftl_mngt_fail_step(mngt);
		return;
	}

	dev->admin_poller_ctx = ctx;

	ftl_property_register(dev, "idle", &ctx->idle, sizeof(ctx->idle),
			      "", "No IO activity and idle state detected",
			      ftl_property_dump_bool,
			      NULL, NULL, false);

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

bool
ftl_is_idle(struct spdk_ftl_dev *dev)
{
	if (dev->admin_poller_ctx) {
		return dev->admin_poller_ctx->idle;
	} else {
		return false;
	}
}
