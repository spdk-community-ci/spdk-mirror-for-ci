/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Advanced Micro Devices, Inc.
 *   All rights reserved.
 */

/**
 * AE4DMA specification definitions
 */

#ifndef SPDK_AE4DMA_SPEC_H
#define SPDK_AE4DMA_SPEC_H

#include "spdk/stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "spdk/assert.h"


/*
 * AE4DMA Device Details
 */

#define MAX_HW_QUEUES			16
#define MAX_CMD_QLEN			32
#define QINDEX				0

/* Register Mappings */
#define CMD_Q_LEN			32
#define CMD_Q_RUN			BIT(0)
#define CMD_Q_HALT			BIT(1)
#define QUEUE_SIZE_VAL			((ffs(CMD_Q_LEN) - 2) & \
								  CMD_Q_SIZE_MASK)
#define Q_PTR_MASK			(2 << (QUEUE_SIZE_VAL + 5) - 1)
#define Q_DESC_SIZE			sizeof(struct spdk_ae4dma_desc)
#define Q_SIZE(n)			(CMD_Q_LEN * (n))

#define INT_DESC_VALIDATED		(1 << 1)
#define INT_DESC_PROCESSED		(1 << 2)
#define INT_COMPLETION			(1 << 3)
#define INT_ERROR			(1 << 4)

#define SUPPORTED_INTERRUPTS		(INT_COMPLETION | INT_ERROR)

/* Descriptor status */
#define DESC_SUBMITTED 0x1
#define DESC_VALIDATED 0x2
#define DESC_COMPLETED 0x3
#define DESC_ERROR     0x4


#define CMD_DESC_DW0_VAL		0x000002
#define CMD_QUEUE_ENABLE	0x1

/* Offset of each(i) queue */
#define QUEUE_START_OFFSET(i) ((i + 1) * 0x20)

/** Common to all queues */
#define COMMON_Q_CONFIG 0x00

#define PCI_BAR 0

/*
 * descriptor for AE4DMA commands
 * 8 32-bit words:
 * word 0: function; engine; control bits
 * word 1: length of source data
 * word 2: low 32 bits of source pointer
 * word 3: upper 16 bits of source pointer; source memory type
 * word 4: low 32 bits of destination pointer
 * word 5: upper 16 bits of destination pointer; destination memory type
 * word 6: reserved 32 bits
 * word 7: reserved 32 bits
 */

#define DWORD0_SOC	BIT(0)
#define DWORD0_IOC	BIT(1)
#define DWORD0_SOM	BIT(3)
#define DWORD0_EOM	BIT(4)
#define DWORD0_DMT	GENMASK(5, 4)
#define DWORD0_SMT	GENMASK(7, 6)

#define DWORD0_DMT_MEM	0x0
#define DWORD0_DMT_IO	1<<4
#define DWORD0_SMT_MEM	0x0
#define DWORD0_SMT_IO	1<<6



union dwou {
	uint32_t dw0;
	struct dword0 {
		uint8_t	byte0;
		uint8_t	byte1;
		uint16_t	timestamp;
	} dws;
};

struct spdk_desc_dword1 {
	uint8_t	status;
	uint8_t	err_code;
	uint16_t	desc_id;
};

struct spdk_ae4dma_desc {
	union dwou dwouv;
	struct spdk_desc_dword1 dw1;
	uint32_t length;
	struct spdk_desc_dword1 uu;
	uint32_t src_hi;
	uint32_t src_lo;
	uint32_t dst_hi;
	uint32_t dst_lo;
};
SPDK_STATIC_ASSERT(sizeof(struct spdk_ae4dma_desc) == 32, "incorrect ae4dma_hw_desc layout");

/*
 * regs for each queue :4 bytes len
 * effective addr:offset+reg
 */
#define AE4DMA_REG_CONTROL		0x00
#define AE4DMA_REG_STATUS		0x04
#define AE4DMA_REG_MAX_IDX		0x08
#define AE4DMA_REG_READ_IDX		0x0C
#define AE4DMA_REG_WRITE_IDX		0x10
#define AE4DMA_REG_INTR_STATUS		0x14
#define AE4DMA_REG_QBASE_LO		0x18
#define AE4DMA_REG_QBASE_HI		0x1C

struct spdk_ae4dma_hwq_regs {
	union {
		uint32_t control_raw;
		struct {
			uint32_t queue_enable: 1;
			uint32_t reserved_internal: 31;
		} control;
	} control_reg;

	union {
		uint32_t status_raw;
		struct {
			uint32_t reserved0: 1;
			uint32_t queue_status: 2; /* 0–empty, 1–full, 2–stopped, 3–error , 4–Not Empty */
			uint32_t reserved1: 21;
			uint32_t interrupt_type: 4;
			uint32_t reserved2: 4;
		} status;
	} status_reg;

	uint32_t max_idx;
	uint32_t write_idx;
	uint32_t read_idx;

	union {
		uint32_t intr_status_raw;
		struct {
			uint32_t intr_status: 1;
			uint32_t reserved: 31;
		} intr_status;
	} intr_status_reg;

	uint32_t qbase_lo;
	uint32_t qbase_hi;

} __attribute__((packed)) __attribute__((aligned));


#ifdef __cplusplus
}
#endif

#endif /* SPDK_AE4DMA_SPEC_H */
