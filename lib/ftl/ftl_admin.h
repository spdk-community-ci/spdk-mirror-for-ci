/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright 2023 Solidigm All Rights Reserved
 */

#ifndef FTL_ADMIN_H
#define FTL_ADMIN_H

struct ftl_nv_cache_chunk;
struct spdk_ftl_dev;

void ftl_admin_nv_cache_throttle_update(struct spdk_ftl_dev *dev);

#endif /* FTL_ADMIN_H */
