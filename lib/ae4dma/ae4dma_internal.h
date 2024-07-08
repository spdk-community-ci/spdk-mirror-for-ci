/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Advanced Micro Devices, Inc.
 *   All rights reserved.
 */

#ifndef __AE4DMA_INTERNAL_H__
#define __AE4DMA_INTERNAL_H__

#include "spdk/stdinc.h"

#include "spdk/ae4dma.h"
#include "spdk/ae4dma_spec.h"
#include "spdk/queue.h"
#include "spdk/mmio.h"
/**
 * upper_32_bits - return bits 32-63 of a number
 * @n: the number we're accessing
 */
#define upper_32_bits(n) ((uint32_t)(((n) >> 16) >> 16))

/**
 * lower_32_bits - return bits 0-31 of a number
 * @n: the number we're accessing
 */
#define lower_32_bits(n) ((uint32_t)((n) & 0xffffffff))

#define AE4DMA_DESCRITPTORS_PER_CMDQ 32
#define AE4DMA_QUEUE_DESC_SIZE  sizeof(struct spdk_ae4dma_desc)
#define AE4DMA_QUEUE_SIZE(n)  (AE4DMA_CMD_QUEUE_LEN * (n))

struct ae4dma_descriptor {
	spdk_ae4dma_req_cb	callback_fn;
	void			*callback_arg;
};

struct ae4dma_cmd_queue {

	volatile struct spdk_ae4dma_hwq_regs *regs;
<<<<<<< HEAD
=======
>>>>>>> acd222ae3 (AE4DMA : Support for multiple hardware queues added)

	/* Queue base address */
	struct spdk_ae4dma_desc *qbase_addr;

	struct spdk_ae4dma_desc *desc_ring;
	struct ae4dma_descriptor *ring;

	uint64_t head;
	uint64_t tail;
<<<<<<< HEAD
<<<<<<< HEAD
=======
>>>>>>> a09489954 (AE4DMA : Integrated with accel framework)
	uint32_t desc_index;

	unsigned int queue_size;
	uint64_t qring_buffer_pa;
	uint64_t qdma_tail;

	/* Queue Statistics */
	uint64_t q_cmd_count;
	uint32_t write_index;
	uint32_t ring_buff_count;
=======
>>>>>>> acd222ae3 (AE4DMA : Support for multiple hardware queues added)

	volatile unsigned long desc_id_counter;

};

struct spdk_ae4dma_chan {
	/* Opaque handle to upper layer */
	struct    spdk_pci_device *device;
	uint64_t  max_xfer_size;

<<<<<<< HEAD
	uint32_t	hwq_index;
	uint32_t        cp_hwq_index;
=======
>>>>>>> a09489954 (AE4DMA : Integrated with accel framework)

	/* I/O area used for device communication */
	void *io_regs;

	unsigned int cmd_count;
	struct ae4dma_cmd_queue cmd_q[AE4DMA_MAX_HW_QUEUES];
	unsigned int cmd_q_count;

	uint32_t	dma_capabilities;

	/* tailq entry for attached_chans */
	TAILQ_ENTRY(spdk_ae4dma_chan)	tailq;
};

#endif /* __AE4DMA_INTERNAL_H__ */
