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

#define AE4DMA_DEFAULT_ORDER	5
#define AE4DMA_DESCRITPTORS_PER_ENGINE 32

struct ae4dma_descriptor {
	uint64_t		phys_addr;
	spdk_ae4dma_req_cb	callback_fn;
	void			*callback_arg;
};
struct ae4dma_completion_event {
	spdk_ae4dma_req_cb	callback_fn;
	void			*callback_arg;
};


struct ae4dma_cmd_queue {

	/* Queue identifier */
	uint32_t id;

	struct	ae4dma_completion_event *cevent;

	/* Queue base address */
	struct spdk_ae4dma_desc *qbase;

	volatile unsigned long qidx;
	volatile unsigned long ridx;

	unsigned int qsize;
	uint64_t qbase_dma;
	uint64_t qdma_tail;

	unsigned int active;
	unsigned int suspended;

	/* Interrupt flag */
	bool int_en;

	/* Register addresses for queue */
	void  *reg_control;
	uint32_t  qcontrol; /* Cached control register */

	/* Status values from job */
	uint32_t  int_status;
	uint32_t  q_status;
	uint32_t  q_int_status;
	uint32_t  cmd_error;
	uint32_t dridx;

	/* Queue Statistics */
	unsigned long total_pt_ops;
	uint64_t	 q_cmd_count;
	uint32_t tail_wi;

	volatile unsigned long desc_id_counter;

};

struct spdk_ae4dma_chan {
	/* Opaque handle to upper layer */
	struct spdk_pci_device		*device;
	uint64_t            max_xfer_size;
	volatile struct spdk_ae4dma_registers *regs;

	uint64_t            head;
	uint64_t            tail;

	uint64_t            last_seen;
	uint64_t            ring_size_order;

	/* I/O area used for device communication */
	void *io_regs;

	unsigned int cmd_count;
	struct ae4dma_descriptor		*ring;
	struct spdk_ae4dma_desc			*hw_ring;
	struct ae4dma_cmd_queue cmd_q[MAX_HW_QUEUES];
	unsigned int cmd_q_count;

	uint32_t	dma_capabilities;

	/* tailq entry for attached_chans */
	TAILQ_ENTRY(spdk_ae4dma_chan)	tailq;
};

static inline uint32_t
is_ae4dma_active(uint64_t status)
{
	return 0;
}

static inline uint32_t
is_ae4dma_idle(uint64_t status)
{
	return 0;
}

static inline uint32_t
is_ae4dma_halted(uint64_t status)
{
	return 0;
}

static inline uint32_t
is_ae4dma_suspended(uint64_t status)
{
	return 0;
}

#endif /* __AE4DMA_INTERNAL_H__ */
