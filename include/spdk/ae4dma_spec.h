/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Advanced Micro Devices, Inc.
 *   All rights reserved.
 */

/**
 * \file
 * AE4DMA specification definitions
 */

#ifndef SPDK_AE4DMA_SPEC_H
#define SPDK_AE4DMA_SPEC_H

#include "spdk/stdinc.h"
#include "spdk/assert.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AE4DMA_MMIO_BAR	0

#define AE4DMA_MAX_HW_QUEUES	16
#define AE4DMA_MAX_CMD_QLEN		32

enum spdk_ae4dma_dma_desc_dest_mem_type {
	AE4DMA_DEST_MEM_TYPE_DRAM	= 0,
	AE4DMA_DEST_MEM_TYPE_IOMEM	= 1,
};

enum spdk_ae4dma_dma_desc_src_mem_type {
	AE4DMA_SRC_MEM_TYPE_DRAM	= 0,
	AE4DMA_SRC_MEM_TYPE_IOMEM	= 1,
};

enum spdk_ae4dma_dma_status {
	AE4DMA_DMA_DESC_SUBMITTED	= 1,
	AE4DMA_DMA_DESC_VALIDATED	= 2,
	AE4DMA_DMA_DESC_PROCESSED	= 4,
	AE4DMA_DMA_DESC_COMPLETED	= 8,
	AE4DMA_DMA_DESC_ERROR		= 16,
};

enum spdk_ae4dma_dma_error_code {
	AE4DMA_DMA_SUCCESS		= 0,
	AE4DMA_DMA_INVALID_HEADER	= 1,
	AE4DMA_DMA_INVALID_STATUS	= 2,
	AE4DMA_DMA_INVALID_LENGTH	= 3,
	AE4DMA_DMA_INVALID_SRC_ADDR	= 4,
	AE4DMA_DMA_INVALID_DEST_ADDR	= 5,
	AE4DMA_DMA_INVALID_ALIGNMENT	= 6,
};

union ae4dma_dma_control_bits {
	struct {
		uint32_t halt_on_completion: 1;
		uint32_t interrupt_on_completion: 1;
		uint32_t start_of_message: 1;
		uint32_t end_of_message: 1;
		uint32_t dest_mem_type: 2;
		uint32_t src_mem_type: 2;
		uint32_t reserved: 24;
	};
	uint32_t raw;
};
SPDK_STATIC_ASSERT(sizeof(union ae4dma_dma_control_bits) == 4, "size mismatch");

struct spdk_ae4dma_dma_hw_desc {
	union ae4dma_dma_control_bits dma_control_bits;
	uint8_t dma_status;
	uint8_t dma_error_code;
	uint16_t desc_id;
	uint32_t length;
	uint32_t reserved;
	uint64_t src_addr;
	uint64_t dest_addr;
};
SPDK_STATIC_ASSERT(sizeof(struct spdk_ae4dma_dma_hw_desc) == 32, "size mismatch");

union ae4dma_queue_control_reg {
	struct {
		uint32_t queue_enable: 1;
		uint32_t reserved: 31;
	};
	uint32_t raw;
};
SPDK_STATIC_ASSERT(sizeof(union ae4dma_queue_control_reg) == 4, "size mismatch");

union ae4dma_queue_status_reg {
	struct {
		uint32_t reserved0: 1;
		uint32_t queue_status: 3;
		uint32_t reserved1: 20;
		uint32_t interrupt_type: 4;
		uint32_t reserved2: 4;
	};
	uint32_t raw;
};
SPDK_STATIC_ASSERT(sizeof(union ae4dma_queue_status_reg) == 4, "size mismatch");

union ae4dma_queue_interrupt_status_reg {
	struct {
		uint32_t interrupt_status: 1;
		uint32_t reserved: 31;
	};
	uint32_t raw;
};
SPDK_STATIC_ASSERT(sizeof(union ae4dma_queue_interrupt_status_reg) == 4, "size mismatch");

/* AE4DMA HW queue registers. 1 for each of the 16 queues. */
struct spdk_ae4dma_hw_queue_reg {
	union ae4dma_queue_control_reg control_reg;
	union ae4dma_queue_status_reg status_reg;
	uint32_t max_index;
	uint32_t read_index;
	uint32_t write_index;
	union ae4dma_queue_interrupt_status_reg interrupt_status_reg;
	uint32_t queue_base_addr_low;
	uint32_t queue_base_addr_high;
};
SPDK_STATIC_ASSERT(sizeof(struct ae4dma_hw_queue_reg) == 32, "size mismatch");

union ae4dma_queue_config_reg {
	struct {
		uint32_t num_queues: 5;
		uint32_t reserved: 27;
	};
	uint32_t raw;
};
SPDK_STATIC_ASSERT(sizeof(union ae4dma_queue_config_reg) == 4, "size mismatch");

/* AE4DMA HW registers that are be accessed when BAR0 addres is mmapped */
struct spdk_ae4dma_hw_registers {
	union ae4dma_queue_config_reg queue_config;
	uint8_t		reserved[28];
	struct spdk_ae4dma_hw_queue_reg hw_queues[AE4DMA_MAX_HW_QUEUES];
};


#ifdef __cplusplus
}
#endif

#endif /* SPDK_AE4DMA_SPEC_H */
