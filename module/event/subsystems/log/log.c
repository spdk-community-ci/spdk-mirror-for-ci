/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2024 Intel Corporation.  All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk/log.h"
#include "spdk_internal/init.h"

static void
log_subsystem_init(void)
{
	int rc = 0;

	spdk_subsystem_init_next(rc);
}

static void
log_subsystem_fini(void)
{
	spdk_subsystem_fini_next();
}

void
spdk_log_write_config(struct spdk_json_write_ctx *w)
{
}

static void
log_subsystem_write_config_json(struct spdk_json_write_ctx *w)
{
	spdk_json_write_array_begin(w);
	spdk_log_write_config(w);
	spdk_json_write_array_end(w);
}

static struct spdk_subsystem g_subsystem_log = {
	.name = "log",
	.init = log_subsystem_init,
	.fini = log_subsystem_fini,
	.write_config_json = log_subsystem_write_config_json,
};

SPDK_SUBSYSTEM_REGISTER(g_subsystem_log);
