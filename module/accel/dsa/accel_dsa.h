/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2022 Intel Corporation.
 *   All rights reserved.
 */

#ifndef SPDK_ACCEL_ENGINE_DSA_H
#define SPDK_ACCEL_ENGINE_DSA_H

#include "spdk/stdinc.h"

enum accel_dsa_driver_type {
	DSA_DRIVER_TYPE_ALL,
	DSA_DRIVER_TYPE_USER,
	DSA_DRIVER_TYPE_KERNEL,
};

struct idxd_probe_opts {
	enum accel_dsa_driver_type driver_type;
};

int accel_dsa_enable_probe(struct idxd_probe_opts *opts);

#endif /* SPDK_ACCEL_ENGINE_DSA_H */
