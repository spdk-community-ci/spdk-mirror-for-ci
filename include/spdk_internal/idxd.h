/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2020 Intel Corporation.
 *   All rights reserved.
 */

#ifndef __IDXD_INTERNAL_H__
#define __IDXD_INTERNAL_H__

#include "spdk/stdinc.h"

#include "spdk/idxd.h"
#include "spdk/queue.h"
#include "spdk/mmio.h"
#include "spdk/bit_array.h"
#include "spdk/util.h"

#ifdef __cplusplus
extern "C" {
#endif

enum dsa_opcode {
	IDXD_OPCODE_NOOP	= 0,
	IDXD_OPCODE_BATCH	= 1,
	IDXD_OPCODE_DRAIN	= 2,
	IDXD_OPCODE_MEMMOVE	= 3,
	IDXD_OPCODE_MEMFILL	= 4,
	IDXD_OPCODE_COMPARE	= 5,
	IDXD_OPCODE_COMPVAL	= 6,
	IDXD_OPCODE_CR_DELTA	= 7,
	IDXD_OPCODE_AP_DELTA	= 8,
	IDXD_OPCODE_DUALCAST	= 9,
	IDXD_OPCODE_CRC32C_GEN	= 16,
	IDXD_OPCODE_COPY_CRC	= 17,
	IDXD_OPCODE_DIF_CHECK	= 18,
	IDXD_OPCODE_DIF_INS	= 19,
	IDXD_OPCODE_DIF_STRP	= 20,
	IDXD_OPCODE_DIF_UPDT	= 21,
	IDXD_OPCODE_DIX_GEN	= 23,
	IDXD_OPCODE_CFLUSH	= 32,
	IDXD_OPCODE_DECOMPRESS	= 66,
	IDXD_OPCODE_COMPRESS	= 67,
};

/**
 * Initialize and move to the first common segment of the two given
 * iovecs. This function will take into account the block size
 * of each iovec and will return common segments having the same
 * number of blocks.
 *
 * All iovec sizes in every array must be a multiple of a given block size,
 * except the last array element, which can be larger. The remaining bytes
 * will be ignored.
 *
 * \param iter iovec iterator
 * \param siov source I/O vector array
 * \param siovcnt size of the source array
 * \param diov destination I/O vector array
 * \param diovcnt size of the destination array
 * \param sblocksize block size of the source array
 * \param dblocksize block size of the destination array
 * \param src returned pointer to the beginning of the segment in the source array buffers
 * \param dst returned pointer to the beginning of the segment in the destination array buffers
 *
 * \return number of blocks in the common segments
 */
size_t spdk_idxd_bioviter_first(struct spdk_ioviter *iter,
				struct iovec *siov, size_t siovcnt,
				struct iovec *diov, size_t diovcnt,
				uint32_t sblocksize, uint32_t dblocksize,
				void **src, void **dst);


/**
 * Move to the next segment in the iterator.
 *
 * This will iterate through the segments of the iovecs in the iterator
 * and return the individual segments, one by one. Selected segments
 * will consist of the same number of blocks.
 *
 * \param iter iovec iterator
 * \param src returned pointer to the next common segment in the source I/O vector array
 * \param dst returned pointer to the next common segment in the destination I/O vector array
 *
 * \return number of blocks in the common segments
 */
uint64_t spdk_idxd_bioviter_next(struct spdk_ioviter *iter, void **src, void **dst);

#ifdef __cplusplus
}
#endif

#endif /* __IDXD_INTERNAL_H__ */
