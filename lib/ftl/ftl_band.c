/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk/crc32.h"
#include "spdk/likely.h"
#include "spdk/util.h"
#include "spdk/ftl.h"

#include "ftl_band.h"
#include "ftl_io.h"
#include "ftl_core.h"
#include "ftl_internal.h"
#include "utils/ftl_md.h"
#include "utils/ftl_defs.h"

static uint64_t
ftl_band_tail_md_offset(const struct ftl_band *band)
{
	return ftl_get_num_blocks_in_band(band->dev) -
	       ftl_tail_md_num_blocks(band->dev);
}

int
ftl_band_filled(struct ftl_band *band, size_t offset)
{
	return offset == ftl_band_tail_md_offset(band);
}

static void
ftl_band_free_p2l_map(struct ftl_band *band)
{
	struct spdk_ftl_dev *dev = band->dev;
	struct ftl_p2l_map *p2l_map = &band->p2l_map;

	assert(band->md->state == FTL_BAND_STATE_CLOSED ||
	       band->md->state == FTL_BAND_STATE_FREE);
	assert(p2l_map->ref_cnt == 0);
	assert(p2l_map->band_map != NULL);

	ftl_mempool_put(dev->p2l_pool, p2l_map->band_map);
	p2l_map->band_map = NULL;
}


static void
ftl_band_free_md_entry(struct ftl_band *band)
{
	struct spdk_ftl_dev *dev = band->dev;
	struct ftl_p2l_map *p2l_map = &band->p2l_map;

	assert(band->md->state == FTL_BAND_STATE_CLOSED ||
	       band->md->state == FTL_BAND_STATE_FREE);
	assert(p2l_map->band_dma_md != NULL);

	ftl_mempool_put(dev->band_md_pool, p2l_map->band_dma_md);
	p2l_map->band_dma_md = NULL;
}

ftl_addr
ftl_band_tail_md_addr(struct ftl_band *band)
{
	ftl_addr addr;

	/* Metadata should be aligned to xfer size */
	assert(ftl_band_tail_md_offset(band) % band->dev->xfer_size == 0);

	addr = ftl_band_tail_md_offset(band) + band->start_addr;

	return addr;
}

void
ftl_band_set_type(struct ftl_band *band, enum ftl_band_type type)
{
	switch (type) {
	case FTL_BAND_TYPE_COMPACTION:
	case FTL_BAND_TYPE_GC:
		band->md->type = type;
		break;
	default:
		assert(false);
		break;
	}
}

void
ftl_band_set_addr(struct ftl_band *band, uint64_t lba, ftl_addr addr)
{
	struct ftl_p2l_map *p2l_map = &band->p2l_map;
	uint64_t offset;

	offset = ftl_band_block_offset_from_addr(band, addr);

	p2l_map->band_map[offset] = lba;
	p2l_map->num_valid++;
}

size_t
ftl_band_user_blocks_left(const struct ftl_band *band, size_t offset)
{
	size_t tail_md_offset = ftl_band_tail_md_offset(band);

	if (spdk_unlikely(offset > tail_md_offset)) {
		return 0;
	}

	return tail_md_offset - offset;
}

struct ftl_band *
ftl_band_from_addr(struct spdk_ftl_dev *dev, ftl_addr addr)
{
	size_t band_id = ftl_addr_get_band(dev, addr);

	assert(band_id < ftl_get_num_bands(dev));
	return &dev->bands[band_id];
}

uint64_t
ftl_band_block_offset_from_addr(struct ftl_band *band, ftl_addr addr)
{
	assert(ftl_addr_get_band(band->dev, addr) == band->id);
	return addr % ftl_get_num_blocks_in_band(band->dev);
}

ftl_addr
ftl_band_next_xfer_addr(struct ftl_band *band, ftl_addr addr, size_t num_blocks)
{
	struct spdk_ftl_dev *dev = band->dev;
	size_t num_xfers;
	uint64_t offset;

	assert(ftl_addr_get_band(dev, addr) == band->id);

	offset = addr - band->start_addr;

	/* In case starting address wasn't aligned to xfer_size, we'll align for consistent calculation
	 * purposes - the unaligned value will be preserved at the end however.
	 */
	num_blocks += (offset % dev->xfer_size);
	offset -= (offset % dev->xfer_size);

	/* Calculate offset based on xfer_size aligned writes */
	num_xfers = (num_blocks / dev->xfer_size);
	offset += num_xfers * dev->xfer_size;
	num_blocks -= num_xfers * dev->xfer_size;

	if (offset > ftl_get_num_blocks_in_band(dev)) {
		return FTL_ADDR_INVALID;
	}

	/* If there's any unalignment (either starting addr value or num_blocks), reintroduce it to the final address
	 */
	if (num_blocks) {
		offset += num_blocks;
		if (offset > ftl_get_num_blocks_in_band(dev)) {
			return FTL_ADDR_INVALID;
		}
	}

	addr = band->start_addr + offset;
	return addr;
}

ftl_addr
ftl_band_addr_from_block_offset(struct ftl_band *band, uint64_t block_off)
{
	ftl_addr addr;

	addr = block_off + band->id * ftl_get_num_blocks_in_band(band->dev);
	return addr;
}

ftl_addr
ftl_band_next_addr(struct ftl_band *band, ftl_addr addr, size_t offset)
{
	uint64_t block_off = ftl_band_block_offset_from_addr(band, addr);

	return ftl_band_addr_from_block_offset(band, block_off + offset);
}

void
ftl_band_acquire_p2l_map(struct ftl_band *band)
{
	assert(band->p2l_map.band_map != NULL);
	band->p2l_map.ref_cnt++;
}

static int
ftl_band_alloc_md_entry(struct ftl_band *band)
{
	struct spdk_ftl_dev *dev = band->dev;
	struct ftl_p2l_map *p2l_map = &band->p2l_map;
	struct ftl_layout_region *region = &dev->layout.region[FTL_LAYOUT_REGION_TYPE_BAND_MD];

	p2l_map->band_dma_md = ftl_mempool_get(dev->band_md_pool);

	if (!p2l_map->band_dma_md) {
		return -1;
	}

	memset(p2l_map->band_dma_md, 0, region->entry_size * FTL_BLOCK_SIZE);
	return 0;
}

int
ftl_band_alloc_p2l_map(struct ftl_band *band)
{
	struct spdk_ftl_dev *dev = band->dev;
	struct ftl_p2l_map *p2l_map = &band->p2l_map;

	assert(p2l_map->ref_cnt == 0);
	assert(p2l_map->band_map == NULL);

	p2l_map->band_map = ftl_mempool_get(dev->p2l_pool);
	if (!p2l_map->band_map) {
		return -1;
	}

	if (ftl_band_alloc_md_entry(band)) {
		ftl_band_free_p2l_map(band);
		return -1;
	}

	/* Set the P2L to FTL_LBA_INVALID */
	memset(p2l_map->band_map, -1, FTL_BLOCK_SIZE * ftl_p2l_map_num_blocks(band->dev));

	ftl_band_acquire_p2l_map(band);
	return 0;
}

void
ftl_band_release_p2l_map(struct ftl_band *band)
{
	struct ftl_p2l_map *p2l_map = &band->p2l_map;

	assert(p2l_map->band_map != NULL);
	assert(p2l_map->ref_cnt > 0);
	p2l_map->ref_cnt--;

	if (p2l_map->ref_cnt == 0) {
		ftl_band_free_p2l_map(band);
		ftl_band_free_md_entry(band);
	}
}

ftl_addr
ftl_band_p2l_map_addr(struct ftl_band *band)
{
	return band->tail_md_addr;
}

int
ftl_band_write_prep(struct ftl_band *band)
{
	if (ftl_band_alloc_p2l_map(band)) {
		return -1;
	}

	ftl_band_iter_init(band);

	return 0;
}

size_t
ftl_p2l_map_pool_elem_size(struct spdk_ftl_dev *dev)
{
	/* Map pool element holds the whole tail md */
	return ftl_tail_md_num_blocks(dev) * FTL_BLOCK_SIZE;
}
