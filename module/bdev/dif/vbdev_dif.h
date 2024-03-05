/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   All rights reserved.
 */

#ifndef SPDK_VBDEV_DIF_H
#define SPDK_VBDEV_DIF_H

#include "spdk/stdinc.h"

#include "spdk/bdev.h"
#include "spdk/bdev_module.h"

/**
 * Create new DIF through bdev.
 *
 * \param bdev_name Bdev on which DIF vbdev will be created.
 * \param vbdev_name Name of the DIF bdev.
 * \param uuid Optional UUID to assign to the DIF bdev.
 * \param dif_insert_or_strip If true, bdev will insert and strip DIFs
 * \return 0 on success, other on failure.
 */
int bdev_dif_create_disk(const char *bdev_name, const char *vbdev_name,
			      const struct spdk_uuid *uuid, bool dif_insert_or_strip);

/**
 * Delete DIF bdev.
 *
 * \param bdev_name Name of the DIF bdev.
 * \param cb_fn Function to call after deletion.
 * \param cb_arg Argument to pass to cb_fn.
 */
void bdev_dif_delete_disk(const char *bdev_name, spdk_bdev_unregister_cb cb_fn,
			       void *cb_arg);

#endif /* SPDK_VBDEV_DIF_H */
