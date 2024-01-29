/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright 2023 Solidigm All Rights Reserved
 *   All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk/thread.h"
#include "spdk/ftl.h"

#include "ftl_admin.h"
#include "ftl_core.h"
#include "utils/ftl_log.h"
#include "mngt/ftl_mngt_steps.h"

#define FTL_ADMIN_POLLER_PERIOD_US (1000UL * 5UL) /* 50 ms */
#define FTL_IDLE_TIME_THRESHOLD_S 60UL /* 60 s */
#define FTL_ADMIN_SMA_ITEM_MIN 100

struct sma_item {
	uint64_t value;
	uint64_t tsc_start;
	uint64_t tsc_stop;
};

struct sma {
	struct sma_item		*items;
	uint64_t		count;
	uint64_t		iter;
	double			value;
	uint64_t		tsc_last;
};

static int
sma_init(struct sma *sma, uint64_t count)
{
	sma->items = calloc(count, sizeof(*sma->items));
	if (!sma->items) {
		return -ENOMEM;
	}

	sma->count = count;
	return 0;
}

static void
sma_deinit(struct sma *sma)
{
	free(sma->items);
	sma->items = NULL;
}

static void
sma_add(struct sma *sma, uint64_t value)
{
	struct sma_item *item;

	if (0 == sma->tsc_last) {
		sma->tsc_last = spdk_thread_get_last_tsc(spdk_get_thread());
		return;
	}

	sma->iter++;
	if (sma->iter >= sma->count) {
		sma->iter = 0;
	}

	item = &sma->items[sma->iter];
	item->value = value;
	item->tsc_start = sma->tsc_last;
	item->tsc_stop = spdk_get_ticks();
	sma->tsc_last = item->tsc_stop;
}

static void
sma_stop(struct sma *sma)
{
	sma->tsc_last = 0;
}

static void
sma_update(struct sma *sma)
{
	uint64_t period = 0;
	uint64_t value = 0;

	for (uint64_t i = 0; i < sma->count; ++i) {
		uint64_t tsc_start = sma->items[i].tsc_start;
		uint64_t tsc_stop = sma->items[i].tsc_stop;

		if (tsc_start == 0 || tsc_stop == 0) {
			continue;
		}

		period += tsc_stop - tsc_start;
		value += sma->items[i].value;
	}

	if (period) {
		sma->value = (double)value / (double)period;
	} else {
		sma->value = 0.0L;
	}
}

struct ftl_admin_nvc {
	bool				throttling_enabled;
	bool				throttling_active;
	uint64_t			throttling_bandwidth;
	uint64_t			chunk_threshold_throttling_start;
	uint64_t			chunk_threshold_throttling_stop;

	/* Moving average of recent compaction velocity */
	struct sma			sma;
	double				throttling_ratio;
};

struct ftl_admin_poller_ctx {
	/* Admin poller */
	struct spdk_poller		*poller;

	/* This flag indicates FTL idle time - no user writes in the given time window */
	bool				idle;

	uint64_t			interval_tsc;

	/* Last IO activity count */
	uint64_t			last_ios;

	uint64_t			last_tsc;

	uint64_t			sum_tsc;

	uint64_t			idle_tsc;

	struct ftl_admin_nvc		nvc;
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
admin_task_nvc_throttling_init(struct spdk_ftl_dev *dev,
			       struct ftl_admin_poller_ctx *ctx)
{
	struct ftl_admin_nvc *admin = &ctx->nvc;
	int rc;
	uint64_t percent, usable_chunks = dev->nv_cache.chunk_usable_count,
			  free_target = dev->conf.nv_cache.chunk_free_target,
			  compaction_threshold = dev->conf.nv_cache.chunk_compaction_threshold;

	percent = compaction_threshold + free_target;
	admin->chunk_threshold_throttling_start = usable_chunks * percent / 100;

	percent = compaction_threshold - free_target;
	admin->chunk_threshold_throttling_stop = usable_chunks * percent / 100;

	if (admin->chunk_threshold_throttling_stop == admin->chunk_threshold_throttling_start) {
		admin->chunk_threshold_throttling_stop--;
	}

	FTL_DEBUGLOG(dev, "NV Cache throttling, start: %lu, stop: %lu\n",
		     admin->chunk_threshold_throttling_start,
		     admin->chunk_threshold_throttling_stop);

	rc = sma_init(&admin->sma, dev->conf.limits[SPDK_FTL_LIMIT_START]);
	if (rc) {
		FTL_ERRLOG(dev, "Unable to allocate memory for chunk compaction SMA\n");
		return rc;
	}

	ftl_property_register(dev, "cache.throttling_bandwidth", &admin->throttling_bandwidth,
			      sizeof(admin->throttling_bandwidth),
			      "MiB/s", "NV Cache throttling bandwidth - moving average of compaction speed",
			      ftl_property_dump_uint64, NULL, NULL, false);

	ftl_property_register(dev, "cache.throttling_ratio", &admin->throttling_ratio,
			      sizeof(admin->throttling_ratio),
			      NULL, "NV Cache throttling ratio",
			      ftl_property_dump_double, NULL, NULL, false);

	ftl_property_register_bool_rw(dev, "cache.throttling_enabled", &admin->throttling_enabled,
				      NULL, "Enable or disable NV Cache throttling", false);

	ftl_property_register(dev, "cache.throttling_active", &admin->throttling_active,
			      sizeof(admin->throttling_active),
			      NULL, "It indicates NV Cache throttling is active",
			      ftl_property_dump_bool, NULL, NULL, false);

	admin->throttling_enabled = true;
	return 0;
}

static void
admin_task_nvc_throttling_deinit(struct spdk_ftl_dev *dev,
				 struct ftl_admin_poller_ctx *ctx)
{
	sma_deinit(&ctx->nvc.sma);
}

static void
admin_task_nvc_throttling(struct spdk_ftl_dev *dev,
			  struct ftl_admin_poller_ctx *ctx)
{
	struct ftl_admin_nvc *admin = &ctx->nvc;
	struct ftl_nv_cache *nvc = &dev->nv_cache;
	double free_blocks, free_blocks_target;
	uint64_t limit;

	nvc->throttle.blocks_submitted_limit = UINT64_MAX;
	nvc->throttle.blocks_submitted = 0;
	admin->throttling_ratio = 0.0L;

	if (admin->sma.value == 0.0L || !admin->throttling_enabled) {
		admin->throttling_active = false;
		return;
	}

	if (!admin->throttling_active) {
		if (nvc->chunk_full_count >= admin->chunk_threshold_throttling_start) {
			FTL_DEBUGLOG(dev, "NV cache throttling activated\n");
			admin->throttling_active = true;
		}
		return;
	}

	if (nvc->chunk_full_count < admin->chunk_threshold_throttling_stop) {
		if (admin->throttling_active) {
			FTL_DEBUGLOG(dev, "NV cache throttling deactivated\n");
			admin->throttling_active = true;
		}
		return;
	}

	free_blocks = ftl_nv_cache_free_blocks(dev);
	free_blocks_target = ftl_nv_cache_free_blocks_target(dev);
	admin->throttling_ratio = free_blocks / free_blocks_target;

	/* Update NV cache throttling limits */
	limit = admin->sma.value * dev->admin_poller_ctx->interval_tsc * admin->throttling_ratio;
	nvc->throttle.blocks_submitted_limit = limit;
}

void
ftl_admin_nv_cache_throttle_update(struct spdk_ftl_dev *dev)
{
	struct ftl_nv_cache *nvc = &dev->nv_cache;
	struct ftl_admin_nvc *admin;

	if (NULL == dev->admin_poller_ctx) {
		return;
	}

	admin = &dev->admin_poller_ctx->nvc;

	sma_add(&admin->sma, nvc->chunk_blocks);
	sma_update(&admin->sma);

	FTL_DEBUGLOG(dev, "NV Cache chunk compacted\n");

	/* Update throttling_bandwidth property */
	admin->throttling_bandwidth = admin->sma.value * (double)FTL_BLOCK_SIZE *
				      (double)spdk_get_ticks_hz() / (double)MiB;

	if (!nvc->chunk_comp_count) {
		FTL_DEBUGLOG(dev, "NV cache compaction stopped\n");
		sma_stop(&admin->sma);
	}
}

static int
ftl_admin_poller(void *pctx)
{
	struct spdk_ftl_dev *dev = pctx;
	struct ftl_admin_poller_ctx *ctx = dev->admin_poller_ctx;

	admin_task_detect_idle(dev, ctx);
	admin_task_nvc_throttling(dev, ctx);
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

	ctx->interval_tsc = spdk_get_ticks_hz() * FTL_ADMIN_POLLER_PERIOD_US / 1000000UL;

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

	if (admin_task_nvc_throttling_init(dev, ctx)) {
		free(ctx);
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
		admin_task_nvc_throttling_deinit(dev, ctx);
		if (ctx->poller) {
			spdk_poller_unregister(&ctx->poller);
		}
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
