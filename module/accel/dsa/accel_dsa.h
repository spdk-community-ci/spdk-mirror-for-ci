/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2022 Intel Corporation.
 *   All rights reserved.
 */

#ifndef SPDK_ACCEL_ENGINE_DSA_H
#define SPDK_ACCEL_ENGINE_DSA_H

#include "spdk/stdinc.h"

struct idxd_probe_opts {
	bool kernel_mode;
};

int accel_dsa_enable_probe(struct idxd_probe_opts *opts);

#endif /* SPDK_ACCEL_ENGINE_DSA_H */
