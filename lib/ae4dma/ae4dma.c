/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2015 Intel Corporation.
 *   All rights reserved.
 */

#include "spdk/stdinc.h"

#include "ae4dma_internal.h"

#include "spdk/env.h"
#include "spdk/util.h"
#include "spdk/memory.h"

#include "spdk/log.h"

#define ALIGN_4K 0x1000
#define MAX_RETRY 10

uint32_t max_hw_q = 1;

static int ae4dma_core_queue_full(struct spdk_ae4dma_chan *ae4dma);

struct ae4dma_driver {
	pthread_mutex_t			lock;
	TAILQ_HEAD(, spdk_ae4dma_chan)	attached_chans;
};

static struct ae4dma_driver g_ae4dma_driver = {
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.attached_chans = TAILQ_HEAD_INITIALIZER(g_ae4dma_driver.attached_chans),
};

/* Mapping the PCI BAR */
static int
ae4dma_map_pci_bar(struct spdk_ae4dma_chan *ae4dma)
{
	int rc;
	void *addr;
	uint64_t phys_addr, size;

	rc = spdk_pci_device_map_bar(ae4dma->device, PCI_BAR, &addr, &phys_addr, &size);
	if (rc != 0 || addr == NULL) {
		SPDK_ERRLOG("pci_device_map_range failed with error code %d\n",
			    rc);
		return -1;
	}

	ae4dma->regs = (volatile struct spdk_ae4dma_registers *)addr;

	return 0;
}

static int
ae4dma_unmap_pci_bar(struct spdk_ae4dma_chan *ae4dma)
{
	int rc = 0;
	void *addr = (void *)ae4dma->regs;

	if (addr) {
		rc = spdk_pci_device_unmap_bar(ae4dma->device, 0, addr);
	}
	return rc;
}


static inline uint32_t
ae4dma_get_active(struct spdk_ae4dma_chan *ae4dma)
{
	return (ae4dma->head - ae4dma->tail) & ((AE4DMA_DESCRITPTORS_PER_ENGINE) - 1);
}

static inline uint32_t
ae4dma_get_ring_space(struct spdk_ae4dma_chan *ae4dma)
{
	return (AE4DMA_DESCRITPTORS_PER_ENGINE) - ae4dma_get_active(ae4dma) - 1 ;
}

static uint32_t
ae4dma_get_ring_index(struct spdk_ae4dma_chan *ae4dma, uint32_t index)
{
	return index & (AE4DMA_DESCRITPTORS_PER_ENGINE - 1);
}

static void
ae4dma_submit_single(struct spdk_ae4dma_chan *ae4dma)
{
	ae4dma->head++;
}

void
spdk_ae4dma_flush(struct spdk_ae4dma_chan *ae4dma)
{
	uint32_t index = ae4dma_get_ring_index(ae4dma, ae4dma->head - 1);
	struct spdk_ae4dma_desc *hw_desc;
	uint32_t tail;
	tail = spdk_mmio_read_4(ae4dma->cmd_q[QINDEX].reg_control + AE4DMA_REG_WRITE_IDX);
	hw_desc = &ae4dma->hw_ring[index];

	ae4dma->cmd_q[QINDEX].tail_wi = ((ae4dma->cmd_q[QINDEX].tail_wi) + 1) % MAX_CMD_QLEN ;
	tail = (uint32_t)ae4dma->cmd_q[QINDEX].tail_wi;
	hw_desc->dw1.desc_id = tail;
	spdk_mmio_write_4(ae4dma->cmd_q[QINDEX].reg_control + AE4DMA_REG_WRITE_IDX, tail);
}

static struct ae4dma_descriptor *
ae4dma_prep_copy(struct spdk_ae4dma_chan *ae4dma, uint64_t dst,
		 uint64_t src, uint32_t len)
{
	struct ae4dma_descriptor *desc;
	struct spdk_ae4dma_desc *hw_desc;
	static int index = 0;
	uint32_t i ;

	assert(len <= ae4dma->max_xfer_size);

	if (ae4dma_get_ring_space(ae4dma) < 1) {
		return NULL;
	}

	i = index;
	desc = &ae4dma->ring[i];

	hw_desc = calloc(1, sizeof(struct spdk_ae4dma_desc));
	hw_desc->dwouv.dws.byte0 = 0;//CMD_DESC_DW0_VAL;

	hw_desc->dw1.status = 0;
	hw_desc->dw1.err_code = 0;
	hw_desc->dw1.desc_id  = 0;

	hw_desc->length = len;

	hw_desc->src_lo = upper_32_bits(src);
	hw_desc->src_hi = lower_32_bits(src);
	hw_desc->dst_lo = upper_32_bits(dst);
	hw_desc->dst_hi = lower_32_bits(dst);

	index = (index + 1) % 32 ;
	memcpy(&ae4dma->cmd_q[QINDEX].qbase[i], hw_desc, sizeof(struct spdk_ae4dma_desc));

	ae4dma_submit_single(ae4dma);
	free(hw_desc);
	return desc;

}

int
spdk_ae4dma_submit_copy(struct spdk_ae4dma_chan *ae4dma, void *cb_arg, spdk_ae4dma_req_cb cb_fn,
			void *dst, const void *src, uint64_t nbytes)
{
	int rc;

	rc = spdk_ae4dma_build_copy(ae4dma, cb_arg, cb_fn, dst, src, nbytes);
	if (rc != 0) {
		return rc;
	}

	spdk_ae4dma_flush(ae4dma);

	return 0;
}

int
spdk_ae4dma_build_copy(struct spdk_ae4dma_chan *ae4dma, void *cb_arg, spdk_ae4dma_req_cb cb_fn,
		       void *dst, const void *src, uint64_t nbytes)
{
	struct ae4dma_descriptor        *last_desc;
	uint64_t        remaining, op_size;
	uint64_t        vdst, vsrc;
	uint64_t        pdst_addr, psrc_addr, dst_len, src_len;
	uint32_t        orig_head;

	if (!ae4dma) {
		return -EINVAL;
	}

	if (ae4dma_core_queue_full(ae4dma)) {
		/* HW queue is full, pausing posting the descriptor for the engine to process the previous ones */
		spdk_delay_us(1000);
	}

	orig_head = ae4dma->head;

	vdst = (uint64_t)dst;
	vsrc = (uint64_t)src;


	remaining = nbytes;

	while (remaining) {
		src_len = dst_len = remaining;
		psrc_addr = (uint64_t) spdk_vtophys((void *)vsrc, NULL);
		if (psrc_addr == SPDK_VTOPHYS_ERROR) {
			return -EINVAL;
		}
		pdst_addr = (uint64_t) spdk_vtophys((void *)vdst, NULL);
		if (pdst_addr == SPDK_VTOPHYS_ERROR) {
			return -EINVAL;
		}

		op_size = spdk_min(dst_len, src_len);
		op_size = spdk_min(op_size, ae4dma->max_xfer_size);
		remaining -= op_size;

		last_desc = ae4dma_prep_copy(ae4dma, pdst_addr, psrc_addr, op_size);


		if (remaining == 0 || last_desc == NULL) {
			break;
		}

		vsrc += op_size;
		vdst += op_size;

	}

	if (last_desc) {
		ae4dma->cmd_q[QINDEX].cevent->callback_fn = cb_fn;
		ae4dma->cmd_q[QINDEX].cevent->callback_arg = cb_arg;
	} else {
		/*
		 * Ran out of descriptors in the ring - reset head to leave things as they were
		 * in case we managed to fill out any descriptors.
		 */
		ae4dma->head = orig_head;
		return -ENOMEM;
	}
	return 0;
}

static int
ae4dma_process_channel_events(struct spdk_ae4dma_chan *ae4dma)
{
	struct spdk_ae4dma_desc *hw_desc;
	uint64_t events_count = 0;
	static uint32_t tail = 0 ;
	uint32_t   read_ix;
	uint32_t desc_status;
	uint8_t retry_count = MAX_RETRY;
	uint32_t errorcode;

	if (ae4dma->head == ae4dma->tail) {
		return 0;
	}

	read_ix = spdk_mmio_read_4(ae4dma->cmd_q[QINDEX].reg_control + AE4DMA_REG_READ_IDX);

	while (tail != read_ix) {      /* to process all the pending read_ix from previous run */
		desc_status = 0;
		retry_count = MAX_RETRY;
		do {
			spdk_delay_us(1);
			hw_desc = &ae4dma->cmd_q[QINDEX].qbase[tail];
			desc_status = hw_desc->dw1.status;
			if (desc_status) {
				if (desc_status != DESC_COMPLETED) {
					errorcode = hw_desc->dw1.err_code;
					SPDK_WARNLOG("Desc error code : %d\n", errorcode);
				}

				if (ae4dma->cmd_q[QINDEX].cevent->callback_fn) {
					ae4dma->cmd_q[QINDEX].cevent->callback_fn(ae4dma->cmd_q[QINDEX].cevent->callback_arg);
				}
			}
		} while (!desc_status && retry_count --);
		tail = (tail + 1) % CMD_Q_LEN;
		ae4dma->tail++ ;
	}

	return events_count;
}

static void
ae4dma_channel_destruct(struct spdk_ae4dma_chan *ae4dma)
{
	ae4dma_unmap_pci_bar(ae4dma);

	if (ae4dma->ring) {
		free(ae4dma->ring);
	}

	if (ae4dma->hw_ring) {
		spdk_free(ae4dma->hw_ring);
	}

	if (ae4dma->cmd_q[QINDEX].qbase) {
		spdk_free(ae4dma->cmd_q[QINDEX].qbase);
	}

	if (ae4dma->cmd_q[QINDEX].cevent) {
		free(ae4dma->cmd_q[QINDEX].cevent);
	}
}

uint32_t
spdk_ae4dma_get_max_descriptors(struct spdk_ae4dma_chan *ae4dma)
{
	return AE4DMA_DESCRITPTORS_PER_ENGINE;
}

static int
ae4dma_channel_start(struct spdk_ae4dma_chan *ae4dma)
{
	uint32_t  num_descriptors;
	uint32_t i;
	uint64_t phys_addr;

	void *mmio_base;
	struct ae4dma_cmd_queue *cmd_q;
	uint32_t  dma_addr_lo, dma_addr_hi;
	uint32_t q_per_eng = max_hw_q;

	if (ae4dma_map_pci_bar(ae4dma) != 0) {
		SPDK_ERRLOG("ae4dma_map_pci_bar() failed\n");
		return -1;
	}
	mmio_base = (uint8_t *)ae4dma->regs;

	/* Always support DMA copy */
	ae4dma->dma_capabilities = SPDK_AE4DMA_ENGINE_COPY_SUPPORTED;
	ae4dma->max_xfer_size = 1ULL << 32;

	/* Update the device registers with queue information. */
	spdk_mmio_write_4((mmio_base + COMMON_Q_CONFIG), q_per_eng);
	spdk_mmio_read_4(mmio_base + COMMON_Q_CONFIG);

	ae4dma->io_regs = mmio_base;

	/* Filling up cmd_q; there would be 'n' cmd_q's for 'n' q_per_eng */
	for (i = 0; i < q_per_eng; i++) {

		/* ae4dma core initialisation */
		cmd_q = &ae4dma->cmd_q[i]; /* i th cmd_q(total 16) */
		cmd_q->id = ae4dma->cmd_q_count;/* at start it is zero */
		ae4dma->cmd_q_count++;

		/* Preset some register values (Q size is 32byte (0x20)) */
		cmd_q->reg_control = ae4dma->io_regs +  QUEUE_START_OFFSET(i);

		/* qsize: 32*sizeof(struct ae4dmadma_desc) */
		cmd_q->qsize = Q_SIZE(Q_DESC_SIZE);//

		ae4dma->cmd_q[QINDEX].cevent = (struct  ae4dma_completion_event *)calloc(1,
					       sizeof(struct ae4dma_completion_event));
		if (ae4dma->cmd_q[QINDEX].cevent == NULL) {
			SPDK_ERRLOG("Failed to allocate cevent memory\n");
			return -1;
		}

		/* dma'ble desc address - for each cmd_q  */
		cmd_q->qbase = spdk_zmalloc(CMD_Q_LEN * sizeof(struct spdk_ae4dma_desc), 64, NULL,
					    SPDK_ENV_LCORE_ID_ANY,
					    SPDK_MALLOC_DMA);
		cmd_q->qbase_dma = spdk_vtophys(cmd_q->qbase, NULL);

		if (cmd_q->qbase_dma == SPDK_VTOPHYS_ERROR) {
			SPDK_ERRLOG("Failed to translate descriptor %u to physical address\n", i);
			return -1;
		}

		cmd_q->qidx = 0;
		cmd_q->q_cmd_count = 0;
		cmd_q->dridx = 0;

		spdk_mmio_write_4(cmd_q->reg_control + AE4DMA_REG_READ_IDX, 0);
		spdk_mmio_write_4(cmd_q->reg_control + AE4DMA_REG_WRITE_IDX, 0);
		cmd_q->tail_wi = spdk_mmio_read_4(cmd_q->reg_control + AE4DMA_REG_WRITE_IDX);
	}

	if (ae4dma->cmd_q_count == 0) {
		printf("no command queues available\n");
		return -1;
	}

	printf("BB1.002 AE4DMA\n");

	for (i = 0; i < ae4dma->cmd_q_count; i++) {

		cmd_q = &ae4dma->cmd_q[i];

		cmd_q->qcontrol = 0; /* Start with nothing */

		/* Update the device registers with queue information. */
		/* Max Index (cmd queue length) */
		spdk_mmio_write_4(cmd_q->reg_control + AE4DMA_REG_MAX_IDX, CMD_Q_LEN);

		cmd_q->qdma_tail = cmd_q->qbase_dma;

		dma_addr_lo = lower_32_bits(cmd_q->qdma_tail);
		spdk_mmio_write_4(cmd_q->reg_control + AE4DMA_REG_QBASE_LO, (uint32_t)dma_addr_lo);
		dma_addr_lo = spdk_mmio_read_4(cmd_q->reg_control + AE4DMA_REG_QBASE_LO);

		dma_addr_hi = upper_32_bits(cmd_q->qdma_tail);
		spdk_mmio_write_4(cmd_q->reg_control + AE4DMA_REG_QBASE_HI, (uint32_t)dma_addr_hi);
		dma_addr_hi = spdk_mmio_read_4(cmd_q->reg_control + AE4DMA_REG_QBASE_HI);

		spdk_mmio_write_4(cmd_q->reg_control, CMD_QUEUE_ENABLE);   /* Queue enable */
		spdk_mmio_write_4(cmd_q->reg_control + AE4DMA_REG_INTR_STATUS,
				  0x1); /* Disabling interrupt_status for polling */

	}

	num_descriptors = AE4DMA_DESCRITPTORS_PER_ENGINE;
	ae4dma->ring = calloc(num_descriptors, sizeof(struct ae4dma_descriptor));
	if (!ae4dma->ring) {
		return -1;
	}

	ae4dma->hw_ring = spdk_zmalloc(num_descriptors * sizeof(struct spdk_ae4dma_desc), 64,
				       NULL, SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
	if (!ae4dma->hw_ring) {
		return -1;
	}

	for (i = 0; i < num_descriptors; i++) {
		phys_addr = spdk_vtophys(&ae4dma->hw_ring[i], NULL);
		if (phys_addr == SPDK_VTOPHYS_ERROR) {
			SPDK_ERRLOG("Failed to translate descriptor %u to physical address\n", i);
			return -1;
		}

		ae4dma->ring[i].phys_addr = phys_addr;
	}

	ae4dma->head = 0;
	ae4dma->tail = 0;
	ae4dma->last_seen = 0;

	return 0;
}

static struct spdk_ae4dma_chan *
ae4dma_attach(struct spdk_pci_device *device)
{
	struct spdk_ae4dma_chan *ae4dma;
	uint32_t cmd_reg;

	ae4dma = calloc(1, sizeof(struct spdk_ae4dma_chan));
	if (ae4dma == NULL) {
		return NULL;
	}

	/* Enable PCI busmaster. */
	spdk_pci_device_cfg_read32(device, &cmd_reg, 4);
	cmd_reg |= 0x4;
	spdk_pci_device_cfg_write32(device, cmd_reg, 4);

	ae4dma->device = device;

	if (ae4dma_channel_start(ae4dma) != 0) {
		ae4dma_channel_destruct(ae4dma);
		free(ae4dma);
		return NULL;
	}

	return ae4dma;
}

struct ae4dma_enum_ctx {
	spdk_ae4dma_probe_cb probe_cb;
	spdk_ae4dma_attach_cb attach_cb;
	void *cb_ctx;
};

static int
ae4dma_enum_cb(void *ctx, struct spdk_pci_device *pci_dev)
{
	struct ae4dma_enum_ctx *enum_ctx = ctx;
	struct spdk_ae4dma_chan *ae4dma;

	/* Verify that this device is not already attached */
	TAILQ_FOREACH(ae4dma, &g_ae4dma_driver.attached_chans, tailq) {
		/*
		 * NOTE: This assumes that the PCI abstraction layer will use the same device handle
		 *  across enumerations; we could compare by BDF instead if this is not true.
		 */
		if (pci_dev == ae4dma->device) {
			return 0;
		}
	}

	if (enum_ctx->probe_cb(enum_ctx->cb_ctx, pci_dev)) {
		/*
		 * Since AE4DMA init is relatively quick, just perform the full init during probing.
		 *  If this turns out to be a bottleneck later, this can be changed to work like
		 *  NVMe with a list of devices to initialize in parallel.
		 */
		ae4dma = ae4dma_attach(pci_dev);
		if (ae4dma == NULL) {
			SPDK_ERRLOG("ae4dma_attach() failed\n");
			return -1;
		}

		TAILQ_INSERT_TAIL(&g_ae4dma_driver.attached_chans, ae4dma, tailq);

		enum_ctx->attach_cb(enum_ctx->cb_ctx, pci_dev, ae4dma);
	}

	return 0;
}

int
spdk_ae4dma_probe(void *cb_ctx, spdk_ae4dma_probe_cb probe_cb, spdk_ae4dma_attach_cb attach_cb)
{
	int rc;
	struct ae4dma_enum_ctx enum_ctx;

	pthread_mutex_lock(&g_ae4dma_driver.lock);

	enum_ctx.probe_cb = probe_cb;
	enum_ctx.attach_cb = attach_cb;
	enum_ctx.cb_ctx = cb_ctx;

	rc = spdk_pci_enumerate(spdk_pci_ae4dma_get_driver(), ae4dma_enum_cb, &enum_ctx);

	pthread_mutex_unlock(&g_ae4dma_driver.lock);

	return rc;
}

void
spdk_ae4dma_detach(struct spdk_ae4dma_chan *ae4dma)
{
	struct ae4dma_driver	*driver = &g_ae4dma_driver;

	/* ae4dma should be in the free list (not registered to a thread)
	 * when calling ae4dma_detach().
	 */
	pthread_mutex_lock(&driver->lock);
	TAILQ_REMOVE(&driver->attached_chans, ae4dma, tailq);
	pthread_mutex_unlock(&driver->lock);

	ae4dma_channel_destruct(ae4dma);
	free(ae4dma);
}

static int
ae4dma_core_queue_full(struct spdk_ae4dma_chan *ae4dma)
{

	uint32_t read_index, write_index, queue_status;

	queue_status = spdk_mmio_read_4(ae4dma->cmd_q[QINDEX].reg_control + AE4DMA_REG_STATUS) & 0x0E ;
	read_index  = spdk_mmio_read_4(ae4dma->cmd_q[QINDEX].reg_control + AE4DMA_REG_READ_IDX);
	write_index  = spdk_mmio_read_4(ae4dma->cmd_q[QINDEX].reg_control + AE4DMA_REG_WRITE_IDX);

	queue_status >>= 3;

	if (((CMD_Q_LEN + write_index - read_index) % CMD_Q_LEN)  >= (CMD_Q_LEN - 1)) {
		return 1;
	}

	return 0;
}


int
spdk_ae4dma_process_events(struct spdk_ae4dma_chan *ae4dma)
{
	return ae4dma_process_channel_events(ae4dma);
}
