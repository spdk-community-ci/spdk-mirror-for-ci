/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   All rights reserved.
 *   Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include "spdk/stdinc.h"

#include "queue_internal.h"

#include "spdk/reduce.h"
#include "spdk/env.h"
#include "spdk/string.h"
#include "spdk/bit_array.h"
#include "spdk/util.h"
#include "spdk/log.h"
#include "spdk/memory.h"
#include "spdk/tree.h"
#include "spdk/queue.h"

#include "libpmem.h"

/* Always round up the size of the PM region to the nearest cacheline. */
#define REDUCE_PM_SIZE_ALIGNMENT	64

/* Offset into the backing device where the persistent memory file's path is stored. */
#define REDUCE_BACKING_DEV_PATH_OFFSET	4096

#define REDUCE_EMPTY_MAP_ENTRY	-1ULL

#define REDUCE_NUM_VOL_REQUESTS	256

/* Structure written to offset 0 of both the pm file and the backing device. */
struct spdk_reduce_vol_superblock {
	uint8_t				signature[8];
	struct spdk_reduce_vol_params	params;
	uint8_t				reserved[3983];
};
SPDK_STATIC_ASSERT(sizeof(struct spdk_reduce_vol_superblock) == 4096, "size incorrect");

#define SPDK_REDUCE_SIGNATURE "SPDKREDU"
/* null terminator counts one */
SPDK_STATIC_ASSERT(sizeof(SPDK_REDUCE_SIGNATURE) - 1 ==
		   SPDK_SIZEOF_MEMBER(struct spdk_reduce_vol_superblock, signature), "size incorrect");

#define REDUCE_PATH_MAX 4096

#define REDUCE_ZERO_BUF_SIZE 0x100000

/* REDUCE_META_CACHE_BLOCK_NUM should not less than 2*REDUCE_NUM_VOL_REQUESTS */
#define REDUCE_META_CACHE_BLOCK_NUM	(4 * REDUCE_NUM_VOL_REQUESTS)
#define REDUCE_META_CACHE_RECLAIM_RATIO	20
#define REDUCE_META_INIT_BUF_SIZE	(256 << 10)	/* 256KB */

/**
 * Describes a persistent memory file used to hold metadata associated with a
 *  compressed volume.
 */
struct spdk_reduce_pm_file {
	char			path[REDUCE_PATH_MAX];
	void			*pm_buf;
	int			pm_is_pmem;
	uint64_t		size;
};

#define REDUCE_IO_READV		1
#define REDUCE_IO_WRITEV	2
#define	REDUCE_IO_UNMAP		3

struct spdk_reduce_chunk_map {
	uint32_t		compressed_size;
	uint32_t		reserved;
	uint64_t		io_unit_index[0];
};

struct spdk_reduce_vol_request {
	/**
	 *  Scratch buffer used for uncompressed chunk.  This is used for:
	 *   1) source buffer for compression operations
	 *   2) destination buffer for decompression operations
	 *   3) data buffer when writing uncompressed chunk to disk
	 *   4) data buffer when reading uncompressed chunk from disk
	 */
	uint8_t					*decomp_buf;
	struct iovec				*decomp_buf_iov;

	/**
	 * These are used to construct the iovecs that are sent to
	 *  the decomp engine, they point to a mix of the scratch buffer
	 *  and user buffer
	 */
	struct iovec				decomp_iov[REDUCE_MAX_IOVECS + 2];
	int					decomp_iovcnt;

	/**
	 *  Scratch buffer used for compressed chunk.  This is used for:
	 *   1) destination buffer for compression operations
	 *   2) source buffer for decompression operations
	 *   3) data buffer when writing compressed chunk to disk
	 *   4) data buffer when reading compressed chunk from disk
	 */
	uint8_t					*comp_buf;
	struct iovec				*comp_buf_iov;
	struct iovec				*iov;
	bool					rmw;
	struct spdk_reduce_vol			*vol;
	int					type;
	int					reduce_errno;
	int					iovcnt;
	int					num_backing_ops;
	uint32_t				num_io_units;
	struct spdk_reduce_backing_io           *backing_io;
	struct spdk_reduce_backing_io		*meta_backing_io;
	bool					chunk_is_compressed;
	bool					copy_after_decompress;
	uint64_t				offset;
	uint64_t				logical_map_index;
	uint64_t				length;
	uint64_t				chunk_map_index;
	uint64_t				read_chunk_map_index;
	struct spdk_reduce_chunk_map		*chunk;
	struct spdk_reduce_chunk_map		*prealloc_chunk;
	spdk_reduce_vol_op_complete		cb_fn;
	void					*cb_arg;
	TAILQ_ENTRY(spdk_reduce_vol_request)	tailq;
	RB_ENTRY(spdk_reduce_vol_request)	rbnode;
	struct spdk_reduce_vol_cb_args		backing_cb_args;
};

struct spdk_reduce_vol {
	struct spdk_reduce_vol_params		params;
	struct spdk_reduce_vol_info		info;
	uint32_t				backing_io_units_per_chunk;
	uint32_t				backing_lba_per_io_unit;
	uint32_t				logical_blocks_per_chunk;
	struct spdk_reduce_pm_file		pm_file;
	struct spdk_reduce_backing_dev		*backing_dev;
	struct spdk_reduce_vol_superblock	*backing_super;
	struct spdk_reduce_vol_superblock	*pm_super;
	uint64_t				*pm_logical_map;
	uint64_t				*pm_chunk_maps;

	struct spdk_bit_array			*allocated_chunk_maps;
	/* The starting position when looking for a block from allocated_chunk_maps */
	uint64_t				find_chunk_offset;
	/* Cache free chunks to speed up lookup of free chunk. */
	struct reduce_queue			free_chunks_queue;
	struct spdk_bit_array			*allocated_backing_io_units;
	/* The starting position when looking for a block from allocated_backing_io_units */
	uint64_t				find_block_offset;
	/* Cache free blocks for backing bdev to speed up lookup of free backing blocks. */
	struct reduce_queue			free_backing_blocks_queue;

	struct spdk_reduce_vol_request		*request_mem;
	TAILQ_HEAD(, spdk_reduce_vol_request)	free_requests;
	RB_HEAD(executing_req_tree, spdk_reduce_vol_request) executing_requests;
	TAILQ_HEAD(, spdk_reduce_vol_request)	queued_requests;

	/* Single contiguous buffer used for all request buffers for this volume. */
	uint8_t					*buf_mem;
	struct iovec				*buf_iov_mem;
	/* Single contiguous buffer used for backing io buffers for this volume. */
	uint8_t					*buf_backing_io_mem;

	/* About meta */
	uint32_t				metablocks_num;
	/* A pointer of many 4k DMA mem from huge page */
	uint8_t					*metablock_buf;
	/* A pointer of many mblk desc struct mem. It's iov describe the 4k metablock mem one by one */
	struct reduce_metablock_extent		*metablock_extent_mem;
	TAILQ_HEAD(, reduce_metablock_extent)	metablocks_free;
	RB_HEAD(metablock_cache_tree, reduce_metablock_extent) metablocks_caching;
	TAILQ_HEAD(, reduce_metablock_extent)	metablocks_lru;

	/* use independent mem for meta backingio instead of the mem on req */
	struct reduce_meta_request		*meta_request_buf;
	TAILQ_HEAD(, reduce_meta_request)	free_meta_request;
};

struct reduce_metablock_extent {
	uint32_t				mblk_sn;
	/* pointer to 4K mem */
	struct iovec				iov;
	/* the node to attach free list or lru list */
	TAILQ_ENTRY(reduce_metablock_extent)	tailq;
	/* the node to attach read/write tree */
	RB_ENTRY(reduce_metablock_extent)	rbnode;

	/* the write_meta_req cnt that one metadata write operation catch */
	uint32_t				batch_write_cnt;
	/* the refcnt should be pending_to_supply_read and pending_to_write elem count */
	uint32_t				refcnt;
	/* when multi io to access metablock, callback them by the tailq */
	TAILQ_HEAD(, reduce_meta_request)	pending_to_supply_read;
	TAILQ_HEAD(, reduce_meta_request)	pending_to_write;
};


/* We need 2 iovs during load - one for the superblock, another for the path */
#define LOAD_IOV_COUNT	2

struct reduce_mblk_init_ctx {
	/* unit: bytes */
	void					*meta_init_buf;
	size_t					buf_len;
	/* unit: backing blocklen */
	uint64_t				off_need_init;
	uint64_t				len_need_init;
	uint64_t				off_need_init_next;
	uint32_t				maxlen_per_io;
};

struct reduce_init_load_ctx {
	struct spdk_reduce_vol			*vol;
	struct spdk_reduce_vol_cb_args		backing_cb_args;
	spdk_reduce_vol_op_with_handle_complete	cb_fn;
	void					*cb_arg;
	struct iovec				iov[LOAD_IOV_COUNT];
	void					*path;
	struct spdk_reduce_backing_io           *backing_io;
	struct reduce_mblk_init_ctx		mblk_init_ctx;
};

struct meta_builtin_readv_ctx {
	struct spdk_reduce_vol *vol;
	struct iovec	*iov;
	int		iovcnt;
	uint64_t	offset;
	uint64_t	length;

	spdk_reduce_vol_op_complete cb_fn;
	void *cb_arg;
};

typedef void (*reduce_request_fn)(void *_req, int reduce_errno);

struct meta_builtin_read_chunk_ctx {
	struct spdk_reduce_vol *vol;
	struct spdk_reduce_vol_request *req;
	reduce_request_fn next_fn;
};

struct meta_builtin_request_ctx {
	struct spdk_reduce_vol_request *req;
	void *logicalmap_meta_req;
};

struct meta_builtin_load_ctx {
	struct reduce_init_load_ctx *load_ctx;
};

/* meta read/write operations are async, should save the context before meta opetations */
union reduce_meta_restore_ctx_container {
	struct meta_builtin_readv_ctx readv_ctx;
	struct meta_builtin_read_chunk_ctx read_chunk_ctx;
	struct meta_builtin_request_ctx req_ctx;
	struct meta_builtin_load_ctx load_ctx;
};

struct reduce_meta_request {
	/* the node to attach free_meta_backingio or pending list of struct reduce_metablock_extent */
	TAILQ_ENTRY(reduce_meta_request)	tailq;

	struct spdk_reduce_vol			*vol;
	/* if need or may need to update meta, set the is_write flag */
	bool					is_write;
	/* when we want to update meta, should do read-modify-write about the metablock.
	 * 1. set the supply_read flag, and do read.
	 * 2. read callback, modify the elem on metablock, and reset the supply_read flag. send do write.
	 * 3. write callback, we can judge the meta stage according the supply_read flag.
	 */
	bool					supply_read;
	enum spdk_reduce_meta_type		mtype;
	uint32_t				mblk_sn;
	uint32_t				elem_sn_on_mblk;
	/* A pointer to caching meta block */
	struct reduce_metablock_extent		*mblk_extent;
	union reduce_meta_restore_ctx_container	restore_ctx;

	struct spdk_reduce_vol_cb_args		cb_args;
	struct spdk_reduce_backing_io		backing_io;
};

static void _start_readv_request(struct spdk_reduce_vol_request *req);
static void _start_writev_request(struct spdk_reduce_vol_request *req);
static uint8_t *g_zero_buf;
static int g_vol_count = 0;

/*
 * Allocate extra metadata chunks and corresponding backing io units to account for
 *  outstanding IO in worst case scenario where logical map is completely allocated
 *  and no data can be compressed.  We need extra chunks in this case to handle
 *  in-flight writes since reduce never writes data in place.
 */
#define REDUCE_NUM_EXTRA_CHUNKS 128

static void
_reduce_persist(struct spdk_reduce_vol *vol, const void *addr, size_t len)
{
	if (vol->pm_file.pm_is_pmem) {
		pmem_persist(addr, len);
	} else {
		pmem_msync(addr, len);
	}
}

static uint64_t
_get_pm_logical_map_size(uint64_t vol_size, uint64_t chunk_size)
{
	uint64_t chunks_in_logical_map, logical_map_size;

	chunks_in_logical_map = vol_size / chunk_size;
	logical_map_size = chunks_in_logical_map * sizeof(uint64_t);

	/* Round up to next cacheline. */
	return spdk_divide_round_up(logical_map_size, REDUCE_PM_SIZE_ALIGNMENT) *
	       REDUCE_PM_SIZE_ALIGNMENT;
}

static uint64_t
_get_total_chunks(uint64_t vol_size, uint64_t chunk_size)
{
	uint64_t num_chunks;

	num_chunks = vol_size / chunk_size;
	num_chunks += REDUCE_NUM_EXTRA_CHUNKS;

	return num_chunks;
}

static inline uint32_t
_reduce_vol_get_chunk_struct_size(uint64_t backing_io_units_per_chunk)
{
	return sizeof(struct spdk_reduce_chunk_map) + sizeof(uint64_t) * backing_io_units_per_chunk;
}

static uint64_t
_get_pm_total_chunks_size(uint64_t vol_size, uint64_t chunk_size, uint64_t backing_io_unit_size)
{
	uint64_t io_units_per_chunk, num_chunks, total_chunks_size;

	num_chunks = _get_total_chunks(vol_size, chunk_size);
	io_units_per_chunk = chunk_size / backing_io_unit_size;

	total_chunks_size = num_chunks * _reduce_vol_get_chunk_struct_size(io_units_per_chunk);

	return spdk_divide_round_up(total_chunks_size, REDUCE_PM_SIZE_ALIGNMENT) *
	       REDUCE_PM_SIZE_ALIGNMENT;
}

static struct spdk_reduce_chunk_map *
_reduce_vol_get_chunk_map(struct spdk_reduce_vol *vol, uint64_t chunk_map_index)
{
	uintptr_t chunk_map_addr;

	assert(chunk_map_index < _get_total_chunks(vol->params.vol_size, vol->params.chunk_size));

	chunk_map_addr = (uintptr_t)vol->pm_chunk_maps;
	chunk_map_addr += chunk_map_index *
			  _reduce_vol_get_chunk_struct_size(vol->backing_io_units_per_chunk);

	return (struct spdk_reduce_chunk_map *)chunk_map_addr;
}

static int
_validate_vol_params(struct spdk_reduce_vol_params *params)
{
	if (params->vol_size > 0) {
		/**
		 * User does not pass in the vol size - it gets calculated by libreduce from
		 *  values in this structure plus the size of the backing device.
		 */
		return -EINVAL;
	}

	if (params->chunk_size == 0 || params->backing_io_unit_size == 0 ||
	    params->logical_block_size == 0) {
		return -EINVAL;
	}

	/* Chunk size must be an even multiple of the backing io unit size. */
	if ((params->chunk_size % params->backing_io_unit_size) != 0) {
		return -EINVAL;
	}

	/* Chunk size must be an even multiple of the logical block size. */
	if ((params->chunk_size % params->logical_block_size) != 0) {
		return -1;
	}

	return 0;
}

static uint64_t
_get_vol_size(uint64_t chunk_size, uint64_t backing_dev_size)
{
	uint64_t num_chunks;

	num_chunks = backing_dev_size / chunk_size;
	if (num_chunks <= REDUCE_NUM_EXTRA_CHUNKS) {
		return 0;
	}

	num_chunks -= REDUCE_NUM_EXTRA_CHUNKS;
	return num_chunks * chunk_size;
}

static uint64_t
_get_pm_file_size(struct spdk_reduce_vol_params *params)
{
	uint64_t total_pm_size;

	total_pm_size = sizeof(struct spdk_reduce_vol_superblock);
	total_pm_size += _get_pm_logical_map_size(params->vol_size, params->chunk_size);
	total_pm_size += _get_pm_total_chunks_size(params->vol_size, params->chunk_size,
			 params->backing_io_unit_size);
	return total_pm_size;
}

const struct spdk_uuid *
spdk_reduce_vol_get_uuid(struct spdk_reduce_vol *vol)
{
	return &vol->params.uuid;
}

static uint64_t
_meta_builtin_get_vol_size(uint64_t backing_dev_size, struct spdk_reduce_vol_params *params)
{
	uint64_t num_chunks;

	num_chunks = (backing_dev_size - params->data_region_off) / params->chunk_size;
	num_chunks -= REDUCE_NUM_EXTRA_CHUNKS;

	return num_chunks * params->chunk_size;
}

static uint32_t
_meta_get_mblksn(struct spdk_reduce_vol_params *params, enum spdk_reduce_meta_type mtype,
		 uint64_t elem_sn)
{
	uint32_t mblksn;

	mblksn = params->meta_region_desc[mtype].offset + elem_sn /
		 params->meta_region_desc[mtype].elems_per_mblk;
	assert(mblksn < params->meta_region_desc[mtype].offset + params->meta_region_desc[mtype].length);

	return mblksn;
}

static inline uint32_t
_meta_get_elem_sn_on_mblk(struct spdk_reduce_vol_params *params, enum spdk_reduce_meta_type mtype,
			  uint64_t elem_sn)
{
	return elem_sn % params->meta_region_desc[mtype].elems_per_mblk;
}

static inline void *
_reduce_get_meta_elem_addr(struct reduce_meta_request *meta_req)
{
	return meta_req->mblk_extent->iov.iov_base + meta_req->elem_sn_on_mblk *
	       meta_req->vol->params.meta_region_desc[meta_req->mtype].size_per_elem;
}

/* struct spdk_reduce_vol_params *params although  */
static void
_initialize_vol_pm_pointers(struct spdk_reduce_vol *vol)
{
	uint64_t logical_map_size;

	/* Superblock is at the beginning of the pm file. */
	vol->pm_super = (struct spdk_reduce_vol_superblock *)vol->pm_file.pm_buf;

	/* Logical map immediately follows the super block. */
	vol->pm_logical_map = (uint64_t *)(vol->pm_super + 1);

	/* Chunks maps follow the logical map. */
	logical_map_size = _get_pm_logical_map_size(vol->params.vol_size, vol->params.chunk_size);
	vol->pm_chunk_maps = (uint64_t *)((uint8_t *)vol->pm_logical_map + logical_map_size);
}


static inline bool
_addr_crosses_huge_page(const void *addr, size_t *size)
{
	size_t _size;
	uint64_t rc;

	assert(size);

	_size = *size;
	rc = spdk_vtophys(addr, size);

	return rc == SPDK_VTOPHYS_ERROR || _size != *size;
}

static inline int
_set_buffer(uint8_t **vol_buffer, uint8_t **_addr, uint8_t *addr_range, size_t buffer_size)
{
	uint8_t *addr;
	size_t size_tmp = buffer_size;

	addr = *_addr;

	/* Verify that addr + buffer_size doesn't cross huge page boundary */
	if (_addr_crosses_huge_page(addr, &size_tmp)) {
		/* Memory start is aligned on 2MiB, so buffer should be located at the end of the page.
		 * Skip remaining bytes and continue from the beginning of the next page */
		addr += size_tmp;
	}

	if (addr + buffer_size > addr_range) {
		SPDK_ERRLOG("Vol buffer %p out of range %p\n", addr, addr_range);
		return -ERANGE;
	}

	*vol_buffer = addr;
	*_addr = addr + buffer_size;

	return 0;
}

static inline uint64_t
_reduce_get_meta_req_lba(struct reduce_meta_request *meta_req)
{
	return meta_req->mblk_sn * REDUCE_META_BLKSZ / meta_req->vol->backing_dev->blocklen;
}

static inline uint64_t
_reduce_get_meta_req_lba_count(struct reduce_meta_request *meta_req)
{
	return REDUCE_META_BLKSZ / meta_req->vol->backing_dev->blocklen;
}

static void
_reduce_meta_block_rw_backing(struct reduce_meta_request *meta_req,
			      enum spdk_reduce_backing_io_type io_type)
{
	struct spdk_reduce_vol *vol = meta_req->vol;
	struct spdk_reduce_backing_io *backing_io = &meta_req->backing_io;

	backing_io->dev = vol->backing_dev;
	backing_io->iov = &meta_req->mblk_extent->iov;
	backing_io->iovcnt = 1;
	backing_io->lba = _reduce_get_meta_req_lba(meta_req);
	backing_io->lba_count = _reduce_get_meta_req_lba_count(meta_req);
	backing_io->backing_io_type = io_type;
	vol->backing_dev->submit_backing_io(backing_io);
}

static int
metablock_cmp(struct reduce_metablock_extent *mblk1, struct reduce_metablock_extent *mblk2)
{
	return (mblk1->mblk_sn < mblk2->mblk_sn ? -1 : mblk1->mblk_sn >
		mblk2->mblk_sn);
}
RB_GENERATE_STATIC(metablock_cache_tree, reduce_metablock_extent, rbnode, metablock_cmp);

static inline void
_metablock_lru_update(struct spdk_reduce_vol *vol, struct reduce_metablock_extent *mblkext,
		      bool new_add)
{
	if (!new_add) {
		TAILQ_REMOVE(&vol->metablocks_lru, mblkext, tailq);
	}
	TAILQ_INSERT_TAIL(&vol->metablocks_lru, mblkext, tailq);
}

static inline void
_metablock_lru_delete(struct spdk_reduce_vol *vol, struct reduce_metablock_extent *mblkext)
{
	RB_REMOVE(metablock_cache_tree, &vol->metablocks_caching, mblkext);
	TAILQ_REMOVE(&vol->metablocks_lru, mblkext, tailq);
	TAILQ_INSERT_TAIL(&vol->metablocks_free, mblkext, tailq);
}

/* When the read process is complete, call this function to trigger
 * the callback function registered in the metadata request on pending_to_supply_read.
 * There will be metadata write requests and metadata read requests in the queue.
 *
 * When reading requests on pending_to_supply_read are callbacked, meta_req is released after the callback is completed.
 * When the write request on pending_to_supply_read is callbacked, the metadata cache block data will be updated
 * and the write request will be moved to the pending_to_write queue.
 *
 * After pending_to_supply_read is empty, if pending_to_write is not empty, one metadata write is triggered,
 * and the metadata in multiple write requests is modified and solidify at one time.
 */
static void
_reduce_metablock_cache_read_done_update(struct reduce_meta_request *meta_req, int32_t error)
{
	struct reduce_metablock_extent *mblkext = meta_req->mblk_extent;
	struct reduce_meta_request *meta_req_next, *meta_req_write;
	struct spdk_reduce_vol *vol = meta_req->vol;

	TAILQ_REMOVE(&mblkext->pending_to_supply_read, meta_req, tailq);
	if (meta_req->is_write && !meta_req->supply_read) {
		TAILQ_INSERT_TAIL(&mblkext->pending_to_write, meta_req, tailq);
		mblkext->batch_write_cnt++;
	} else {
		mblkext->refcnt--;
		TAILQ_INSERT_TAIL(&vol->free_meta_request, meta_req, tailq);
	}

	/* check whether collected all modify region */
	meta_req_next = TAILQ_FIRST(&mblkext->pending_to_supply_read);
	if (meta_req_next) {
		SPDK_INFOLOG(reduce,
			     "metareq:%p mblkext %p mblksn %u ref %u. triger next readreq cpcb %p, error %d\n",
			     meta_req, mblkext,
			     mblkext->mblk_sn, mblkext->refcnt, meta_req_next, error);
		assert(mblkext->refcnt);
		meta_req_next->backing_io.backing_cb_args->cb_fn(meta_req_next->backing_io.backing_cb_args->cb_arg,
				error);
	} else {
		/* we have callback all supply read metareq, start write to backend */
		meta_req_write = TAILQ_FIRST(&mblkext->pending_to_write);
		if (meta_req_write) {
			SPDK_INFOLOG(reduce,
				     "metareq:%p mblkext %p mblksn %u ref %u. collect write req cnt %u, triger writereq do write %p\n",
				     meta_req, mblkext, mblkext->mblk_sn, mblkext->refcnt, mblkext->batch_write_cnt, meta_req_write);
			assert(mblkext->refcnt);
			assert(mblkext->batch_write_cnt);
			assert(error == 0);
			_reduce_meta_block_rw_backing(meta_req_write, SPDK_REDUCE_BACKING_IO_WRITE);
		} else {
			if (error != 0) {
				/* This cache block has experienced abnormal reads or writes. Release it */
				assert(mblkext->refcnt == 0);
				assert(mblkext->batch_write_cnt == 0);
				_metablock_lru_delete(vol, mblkext);
			}
		}
	}
}

/* After meta block has written done, callback meta_req in pending_to_write one by one.
 * Of course, during the process of metadata asynchronous disks, there will appear metadata read and write requests,
 * but they will be in pending_to_supply_read.
 * Because the metadata has just been written to backend, the metadata cache block in memory is the same as that on backend,
 * and the requests on the queue can be called back one by one.
 */
static void
_reduce_metablock_cache_write_done_update(struct reduce_meta_request *meta_req, int32_t error)
{
	struct reduce_metablock_extent *mblkext = meta_req->mblk_extent;
	struct reduce_meta_request *meta_req_next;
	struct spdk_reduce_vol *vol = meta_req->vol;

	mblkext->refcnt--;
	mblkext->batch_write_cnt--;

	TAILQ_REMOVE(&mblkext->pending_to_write, meta_req, tailq);
	TAILQ_INSERT_TAIL(&vol->free_meta_request, meta_req, tailq);

	/* trigger next */
	meta_req_next = TAILQ_FIRST(&mblkext->pending_to_write);
	if (meta_req_next) {
		SPDK_INFOLOG(reduce,
			     "metareq:%p mblkext %p mblksn %u ref %u. write done to triger next writereq cpcb %p, error %d\n",
			     meta_req, mblkext,
			     mblkext->mblk_sn, mblkext->refcnt, meta_req_next, error);
		assert(mblkext->refcnt);
		assert(mblkext->batch_write_cnt);
		meta_req_next->backing_io.backing_cb_args->cb_fn(meta_req_next->backing_io.backing_cb_args->cb_arg,
				error);
	} else {
		/* when we async write to backend, there maybe new read come in, callback them */
		assert(mblkext->batch_write_cnt == 0);
		meta_req_next = TAILQ_FIRST(&mblkext->pending_to_supply_read);
		if (meta_req_next) {
			SPDK_INFOLOG(reduce,
				     "metareq:%p mblkext %p mblksn %u ref %u. triger next readreq cpcb %p, error %d\n",
				     meta_req, mblkext,
				     mblkext->mblk_sn, mblkext->refcnt, meta_req_next, error);
			assert(mblkext->refcnt);
			meta_req_next->backing_io.backing_cb_args->cb_fn(meta_req_next->backing_io.backing_cb_args->cb_arg,
					error);
		} else {
			if (error != 0) {
				/* This cache block has experienced abnormal reads or writes. Release it */
				assert(mblkext->refcnt == 0);
				assert(mblkext->batch_write_cnt == 0);
				_metablock_lru_delete(vol, mblkext);
			}
		}
	}
}

static uint32_t
_metablock_lru_reclaim_batch(struct spdk_reduce_vol *vol)
{
	struct reduce_metablock_extent *mblkext, *mblkext_next;
	uint32_t reclaim_num_expected = vol->metablocks_num * REDUCE_META_CACHE_RECLAIM_RATIO / 100;
	uint32_t reclaim_count = 0;

	TAILQ_FOREACH_SAFE(mblkext, &vol->metablocks_lru, tailq, mblkext_next) {
		if (mblkext->refcnt == 0 && reclaim_count < reclaim_num_expected) {
			assert(TAILQ_EMPTY(&mblkext->pending_to_supply_read));
			assert(TAILQ_EMPTY(&mblkext->pending_to_write));
			_metablock_lru_delete(vol, mblkext);
			reclaim_count++;
		} else {
			break;
		}
	}
	SPDK_INFOLOG(reduce, "reclaim count %u\n", reclaim_count);

	return reclaim_count;
}

/* The entry of meta block operation */
static void
_reduce_metablock_cache_read(struct reduce_meta_request *meta_req)
{
	struct reduce_metablock_extent *mblkext_res = NULL;
	struct reduce_metablock_extent mblkext_find;
	struct spdk_reduce_vol *vol = meta_req->vol;

	mblkext_find.mblk_sn = meta_req->mblk_sn;
	mblkext_res = RB_FIND(metablock_cache_tree, &vol->metablocks_caching, &mblkext_find);

	if (mblkext_res) {
		meta_req->mblk_extent = mblkext_res;
		TAILQ_INSERT_TAIL(&mblkext_res->pending_to_supply_read, meta_req, tailq);
		mblkext_res->refcnt++;
		_metablock_lru_update(vol, mblkext_res, false);
		if (mblkext_res->refcnt == 1) {
			/* metablock in cache, just sync cb */
			SPDK_INFOLOG(reduce, "metareq %p mblkext %p mblksn %u ref %u. direct get from cache.\n", meta_req,
				     mblkext_res,
				     mblkext_res->mblk_sn, mblkext_res->refcnt);
			meta_req->backing_io.backing_cb_args->cb_fn(meta_req->backing_io.backing_cb_args->cb_arg, 0);
		} else {
			/* pending wait cb */
			SPDK_INFOLOG(reduce, "metareq %p mblkext %p mblksn %u ref %u. wait callback.\n", meta_req,
				     mblkext_res,
				     mblkext_res->mblk_sn, mblkext_res->refcnt);
		}
	} else {
		mblkext_res = TAILQ_FIRST(&vol->metablocks_free);
		if (!mblkext_res) {
			_metablock_lru_reclaim_batch(vol);
			mblkext_res = TAILQ_FIRST(&vol->metablocks_free);
			assert(mblkext_res);
		}
		TAILQ_REMOVE(&vol->metablocks_free, mblkext_res, tailq);
		assert(mblkext_res->refcnt == 0);
		assert(mblkext_res->batch_write_cnt == 0);

		mblkext_res->mblk_sn = meta_req->mblk_sn;
		RB_INSERT(metablock_cache_tree, &vol->metablocks_caching, mblkext_res);
		_metablock_lru_update(vol, mblkext_res, true);

		TAILQ_INSERT_TAIL(&mblkext_res->pending_to_supply_read, meta_req, tailq);
		mblkext_res->refcnt++;
		meta_req->mblk_extent = mblkext_res;

		SPDK_INFOLOG(reduce, "metareq %p mblkext %p mtype %u mblksn %u ref %u. add to cache\n", meta_req,
			     mblkext_res, meta_req->mtype,
			     mblkext_res->mblk_sn, mblkext_res->refcnt);
		_reduce_meta_block_rw_backing(meta_req, SPDK_REDUCE_BACKING_IO_READ);
	}
}

static int
_allocate_vol_requests(struct spdk_reduce_vol *vol)
{
	struct spdk_reduce_vol_request *req;
	struct spdk_reduce_backing_dev *backing_dev = vol->backing_dev;
	uint32_t reqs_in_2mb_page, huge_pages_needed, request_memlen;
	uint8_t *buffer, *buffer_end;
	int i = 0;
	int rc = 0;

	/* It is needed to allocate comp and decomp buffers so that they do not cross physical
	* page boundaries. Assume that the system uses default 2MiB pages and chunk_size is not
	* necessarily power of 2
	* Allocate 2x since we need buffers for both read/write and compress/decompress
	* intermediate buffers. */
	reqs_in_2mb_page = VALUE_2MB / (vol->params.chunk_size * 2);
	if (!reqs_in_2mb_page) {
		return -EINVAL;
	}
	huge_pages_needed = SPDK_CEIL_DIV(REDUCE_NUM_VOL_REQUESTS, reqs_in_2mb_page);

	vol->buf_mem = spdk_dma_malloc(VALUE_2MB * huge_pages_needed, VALUE_2MB, NULL);
	if (vol->buf_mem == NULL) {
		return -ENOMEM;
	}

	request_memlen = sizeof(*req);
	if (vol->params.meta_builtin) {
		request_memlen = sizeof(*req) + _reduce_vol_get_chunk_struct_size(vol->backing_io_units_per_chunk);
	}
	vol->request_mem = calloc(REDUCE_NUM_VOL_REQUESTS, request_memlen);
	if (vol->request_mem == NULL) {
		spdk_free(vol->buf_mem);
		vol->buf_mem = NULL;
		return -ENOMEM;
	}

	/* Allocate 2x since we need iovs for both read/write and compress/decompress intermediate
	 *  buffers.
	 */
	vol->buf_iov_mem = calloc(REDUCE_NUM_VOL_REQUESTS,
				  2 * sizeof(struct iovec) * vol->backing_io_units_per_chunk);
	if (vol->buf_iov_mem == NULL) {
		free(vol->request_mem);
		spdk_free(vol->buf_mem);
		vol->request_mem = NULL;
		vol->buf_mem = NULL;
		return -ENOMEM;
	}

	vol->buf_backing_io_mem = calloc(REDUCE_NUM_VOL_REQUESTS, (sizeof(struct spdk_reduce_backing_io) +
					 backing_dev->user_ctx_size) * vol->backing_io_units_per_chunk);
	if (vol->buf_backing_io_mem == NULL) {
		free(vol->request_mem);
		free(vol->buf_iov_mem);
		spdk_free(vol->buf_mem);
		vol->request_mem = NULL;
		vol->buf_iov_mem = NULL;
		vol->buf_mem = NULL;
		return -ENOMEM;
	}

	buffer = vol->buf_mem;
	buffer_end = buffer + VALUE_2MB * huge_pages_needed;

	for (i = 0; i < REDUCE_NUM_VOL_REQUESTS; i++) {
		req = (void *)vol->request_mem + i * request_memlen;
		TAILQ_INSERT_HEAD(&vol->free_requests, req, tailq);
		if (vol->params.meta_builtin) {
			req->prealloc_chunk = (void *)req + sizeof(*req);
		}
		req->backing_io = (struct spdk_reduce_backing_io *)(vol->buf_backing_io_mem + i *
				  (sizeof(struct spdk_reduce_backing_io) + backing_dev->user_ctx_size) *
				  vol->backing_io_units_per_chunk);

		req->decomp_buf_iov = &vol->buf_iov_mem[(2 * i) * vol->backing_io_units_per_chunk];
		req->comp_buf_iov = &vol->buf_iov_mem[(2 * i + 1) * vol->backing_io_units_per_chunk];

		rc = _set_buffer(&req->comp_buf, &buffer, buffer_end, vol->params.chunk_size);
		if (rc) {
			SPDK_ERRLOG("Failed to set comp buffer for req idx %u, addr %p, start %p, end %p\n", i, buffer,
				    vol->buf_mem, buffer_end);
			break;
		}
		rc = _set_buffer(&req->decomp_buf, &buffer, buffer_end, vol->params.chunk_size);
		if (rc) {
			SPDK_ERRLOG("Failed to set decomp buffer for req idx %u, addr %p, start %p, end %p\n", i, buffer,
				    vol->buf_mem, buffer_end);
			break;
		}
	}

	if (rc) {
		free(vol->buf_backing_io_mem);
		free(vol->buf_iov_mem);
		free(vol->request_mem);
		spdk_free(vol->buf_mem);
		vol->buf_mem = NULL;
		vol->buf_backing_io_mem = NULL;
		vol->buf_iov_mem = NULL;
		vol->request_mem = NULL;
	}

	return rc;
}

const struct spdk_reduce_vol_info *
spdk_reduce_vol_get_info(const struct spdk_reduce_vol *vol)
{
	return &vol->info;
}

static void
_init_load_cleanup(struct spdk_reduce_vol *vol, struct reduce_init_load_ctx *ctx)
{
	if (ctx != NULL) {
		spdk_free(ctx->path);
		spdk_free(ctx->mblk_init_ctx.meta_init_buf);
		free(ctx->backing_io);
		free(ctx);
	}

	if (vol != NULL) {
		if (vol->pm_file.pm_buf != NULL) {
			pmem_unmap(vol->pm_file.pm_buf, vol->pm_file.size);
		}

		spdk_free(vol->backing_super);
		spdk_bit_array_free(&vol->allocated_chunk_maps);
		spdk_bit_array_free(&vol->allocated_backing_io_units);
		free(vol->request_mem);
		free(vol->buf_backing_io_mem);
		free(vol->buf_iov_mem);
		spdk_free(vol->buf_mem);
		spdk_free(vol->metablock_buf);
		free(vol->metablock_extent_mem);
		free(vol->meta_request_buf);
		free(vol);
	}
}

static int
_alloc_zero_buff(void)
{
	int rc = 0;

	/* The zero buffer is shared between all volumes and just used
	 * for reads so allocate one global instance here if not already
	 * allocated when another vol init'd or loaded.
	 */
	if (g_vol_count++ == 0) {
		g_zero_buf = spdk_zmalloc(REDUCE_ZERO_BUF_SIZE,
					  64, NULL, SPDK_ENV_LCORE_ID_ANY,
					  SPDK_MALLOC_DMA);
		if (g_zero_buf == NULL) {
			g_vol_count--;
			rc = -ENOMEM;
		}
	}
	return rc;
}

static int
_allocate_vol_metablock_and_metaio(struct spdk_reduce_vol *vol)
{
	uint32_t mblk_in_2mb_page, total_mblk, total_metareq, huge_pages_needed, backing_io_struct_size, i;
	struct reduce_metablock_extent *mblk_extent;
	struct reduce_meta_request *meta_request;
	int rc = 0;

	mblk_in_2mb_page = VALUE_2MB / REDUCE_META_BLKSZ;
	huge_pages_needed = SPDK_CEIL_DIV(REDUCE_META_CACHE_BLOCK_NUM, mblk_in_2mb_page);
	total_mblk = huge_pages_needed * mblk_in_2mb_page;
	vol->metablocks_num = total_mblk;

	/* alloc metablock mem */
	vol->metablock_buf = spdk_dma_malloc(VALUE_2MB * huge_pages_needed, VALUE_2MB, NULL);
	if (vol->metablock_buf == NULL) {
		rc = -ENOMEM;
		goto err;
	}

	/* alloc metablock control struct */
	vol->metablock_extent_mem = calloc(total_mblk, sizeof(*vol->metablock_extent_mem));
	if (vol->request_mem == NULL) {
		rc = -ENOMEM;
		goto err;
	}
	for (i = 0; i < total_mblk; i++) {
		mblk_extent = &vol->metablock_extent_mem[i];
		mblk_extent->iov.iov_base = vol->metablock_buf + i * REDUCE_META_BLKSZ;
		mblk_extent->iov.iov_len = REDUCE_META_BLKSZ;
		TAILQ_INIT(&mblk_extent->pending_to_supply_read);
		TAILQ_INIT(&mblk_extent->pending_to_write);
		TAILQ_INSERT_HEAD(&vol->metablocks_free, mblk_extent, tailq);
	}

	/* alloc reduce_meta_request struct mem
	 * Use these struct memory to record the ctx before meta read/write operetion, wait complete callback to restore them.
	 */
	backing_io_struct_size = sizeof(struct reduce_meta_request) + vol->backing_dev->user_ctx_size;
	/* every vol date request may need 2 meta req for logical_map and chunk_map */
	total_metareq = 2 * REDUCE_NUM_VOL_REQUESTS;
	vol->meta_request_buf = (struct reduce_meta_request *)calloc(total_metareq, backing_io_struct_size);
	if (vol->meta_request_buf == NULL) {
		rc = -ENOMEM;
		goto err;
	}
	for (i = 0; i < total_metareq; i++) {
		meta_request = (struct reduce_meta_request *)((uint8_t *)vol->meta_request_buf + i *
				backing_io_struct_size);
		meta_request->backing_io.backing_cb_args = &meta_request->cb_args;
		TAILQ_INSERT_HEAD(&vol->free_meta_request, meta_request, tailq);
	}

	return 0;
err:
	spdk_free(vol->metablock_buf);
	free(vol->metablock_extent_mem);
	free(vol->meta_request_buf);
	vol->metablock_buf = NULL;
	vol->metablock_extent_mem = NULL;
	vol->meta_request_buf = NULL;

	return rc;
}

static void
_init_write_super_cpl(void *cb_arg, int reduce_errno)
{
	struct reduce_init_load_ctx *init_ctx = cb_arg;
	int rc = 0;

	if (reduce_errno != 0) {
		rc = reduce_errno;
		goto err;
	}

	rc = _allocate_vol_requests(init_ctx->vol);
	if (rc != 0) {
		goto err;
	}

	if (init_ctx->vol->params.meta_builtin) {
		rc = _allocate_vol_metablock_and_metaio(init_ctx->vol);
		if (rc != 0) {
			goto err;
		}
	}

	rc = _alloc_zero_buff();
	if (rc != 0) {
		goto err;
	}

	init_ctx->cb_fn(init_ctx->cb_arg, init_ctx->vol, rc);
	/* Only clean up the ctx - the vol has been passed to the application
	 *  for use now that initialization was successful.
	 */
	_init_load_cleanup(NULL, init_ctx);

	return;
err:
	if (unlink(init_ctx->path)) {
		SPDK_ERRLOG("%s could not be unlinked: %s\n",
			    (char *)init_ctx->path, spdk_strerror(errno));
	}

	init_ctx->cb_fn(init_ctx->cb_arg, NULL, rc);
	_init_load_cleanup(init_ctx->vol, init_ctx);
}

static void
_init_write_path_cpl(void *cb_arg, int reduce_errno)
{
	struct reduce_init_load_ctx *init_ctx = cb_arg;
	struct spdk_reduce_vol *vol = init_ctx->vol;
	struct spdk_reduce_backing_io *backing_io = init_ctx->backing_io;

	if (reduce_errno != 0) {
		_init_write_super_cpl(cb_arg, reduce_errno);
		return;
	}

	init_ctx->iov[0].iov_base = vol->backing_super;
	init_ctx->iov[0].iov_len = sizeof(*vol->backing_super);
	init_ctx->backing_cb_args.cb_fn = _init_write_super_cpl;
	init_ctx->backing_cb_args.cb_arg = init_ctx;

	backing_io->dev = vol->backing_dev;
	backing_io->iov = init_ctx->iov;
	backing_io->iovcnt = 1;
	backing_io->lba = 0;
	backing_io->lba_count = sizeof(*vol->backing_super) / vol->backing_dev->blocklen;
	backing_io->backing_cb_args = &init_ctx->backing_cb_args;
	backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_WRITE;

	vol->backing_dev->submit_backing_io(backing_io);
}

static void
_init_builtin_meta_region_cpl(void *cb_arg, int reduce_errno)
{
	struct reduce_init_load_ctx *init_ctx = cb_arg;
	struct spdk_reduce_vol *vol = init_ctx->vol;
	struct spdk_reduce_backing_io *backing_io = init_ctx->backing_io;

	if (reduce_errno != 0) {
		_init_write_path_cpl(cb_arg, reduce_errno);
		return;
	}

	init_ctx->iov[0].iov_base = init_ctx->path;
	init_ctx->iov[0].iov_len = REDUCE_PATH_MAX;
	init_ctx->backing_cb_args.cb_fn = _init_write_path_cpl;
	init_ctx->backing_cb_args.cb_arg = init_ctx;
	/* Write path to offset 4K on backing device - just after where the super
	 *  block will be written.  We wait until this is committed before writing the
	 *  super block to guarantee we don't get the super block written without the
	 *  the path if the system crashed in the middle of a write operation.
	 */
	backing_io->dev = vol->backing_dev;
	backing_io->iov = init_ctx->iov;
	backing_io->iovcnt = 1;
	backing_io->lba = REDUCE_BACKING_DEV_PATH_OFFSET / vol->backing_dev->blocklen;
	backing_io->lba_count = REDUCE_PATH_MAX / vol->backing_dev->blocklen;
	backing_io->backing_cb_args = &init_ctx->backing_cb_args;
	backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_WRITE;

	vol->backing_dev->submit_backing_io(backing_io);
}

static void
_init_builtin_meta_region(void *cb_arg, int reduce_errno)
{
	struct reduce_init_load_ctx *init_ctx = cb_arg;
	struct spdk_reduce_vol *vol = init_ctx->vol;
	struct reduce_mblk_init_ctx *ctx = &init_ctx->mblk_init_ctx;
	struct spdk_reduce_backing_io *backing_io = init_ctx->backing_io;
	uint64_t offset, len;

	if (reduce_errno || ctx->off_need_init_next == ctx->off_need_init + ctx->len_need_init) {
		_init_builtin_meta_region_cpl(cb_arg, reduce_errno);
		return;
	}

	offset = ctx->off_need_init_next;
	len = spdk_min(ctx->maxlen_per_io,
		       ctx->off_need_init + ctx->len_need_init - ctx->off_need_init_next);
	ctx->off_need_init_next = offset + len;

	init_ctx->iov[0].iov_base = ctx->meta_init_buf;
	init_ctx->iov[0].iov_len = len * vol->backing_dev->blocklen;
	init_ctx->backing_cb_args.cb_fn = _init_builtin_meta_region;
	init_ctx->backing_cb_args.cb_arg = init_ctx;

	backing_io->dev = vol->backing_dev;
	backing_io->iov = init_ctx->iov;
	backing_io->iovcnt = 1;
	backing_io->lba = offset;
	backing_io->lba_count = len;
	backing_io->backing_cb_args = &init_ctx->backing_cb_args;
	backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_WRITE;

	vol->backing_dev->submit_backing_io(backing_io);
}

static void
_init_builtin_meta_region_ctx_init(struct reduce_init_load_ctx *init_ctx)
{
	struct reduce_mblk_init_ctx *mblk_init_ctx = &init_ctx->mblk_init_ctx;
	struct spdk_reduce_vol *vol = init_ctx->vol;

	mblk_init_ctx->off_need_init = vol->params.meta_region_desc[REDUCE_MTYPE_SB].offset /
				       REDUCE_META_BLKSZ *
				       vol->backing_dev->blocklen;
	mblk_init_ctx->len_need_init = vol->params.data_region_off / vol->backing_dev->blocklen -
				       mblk_init_ctx->off_need_init;
	mblk_init_ctx->off_need_init_next = mblk_init_ctx->off_need_init;
	mblk_init_ctx->maxlen_per_io = mblk_init_ctx->buf_len / vol->backing_dev->blocklen;
}

static int
_allocate_bit_arrays(struct spdk_reduce_vol *vol)
{
	uint64_t total_chunks, total_backing_io_units;
	uint32_t i, num_metadata_io_units;

	total_chunks = _get_total_chunks(vol->params.vol_size, vol->params.chunk_size);
	if (vol->params.meta_builtin) {
		total_chunks += vol->params.data_region_off / vol->params.chunk_size;
	}
	vol->allocated_chunk_maps = spdk_bit_array_create(total_chunks);
	vol->find_chunk_offset = 0;
	total_backing_io_units = total_chunks * (vol->params.chunk_size / vol->params.backing_io_unit_size);
	vol->allocated_backing_io_units = spdk_bit_array_create(total_backing_io_units);
	vol->find_block_offset = 0;

	if (vol->allocated_chunk_maps == NULL || vol->allocated_backing_io_units == NULL) {
		return -ENOMEM;
	}

	/* Set backing io unit bits associated with metadata. */
	if (vol->params.meta_builtin) {
		num_metadata_io_units = vol->params.data_region_off / vol->params.backing_io_unit_size;
	} else {
		num_metadata_io_units = (sizeof(*vol->backing_super) + REDUCE_PATH_MAX) /
					vol->params.backing_io_unit_size;
	}
	for (i = 0; i < num_metadata_io_units; i++) {
		spdk_bit_array_set(vol->allocated_backing_io_units, i);
		vol->info.allocated_io_units++;
	}

	return 0;
}

static int
overlap_cmp(struct spdk_reduce_vol_request *req1, struct spdk_reduce_vol_request *req2)
{
	return (req1->logical_map_index < req2->logical_map_index ? -1 : req1->logical_map_index >
		req2->logical_map_index);
}
RB_GENERATE_STATIC(executing_req_tree, spdk_reduce_vol_request, rbnode, overlap_cmp);

static inline int
_check_meta_builtin(const char *path)
{
	return strlen(path) == strlen(REDUCE_META_BUILTIN) &&
	       memcmp(path, REDUCE_META_BUILTIN, sizeof(REDUCE_META_BUILTIN)) == 0 ? 1 : 0;
}

static inline uint32_t
_meta_get_elem_struct_size(struct spdk_reduce_vol_params *params, enum spdk_reduce_meta_type mtype)
{
	uint32_t size = 0;

	switch (mtype) {
	case REDUCE_MTYPE_SB:
		size = sizeof(struct spdk_reduce_vol_superblock);
		break;
	case REDUCE_MTYPE_PATH:
		size = REDUCE_PATH_MAX;
		break;
	case REDUCE_MTYPE_LOGICAL_MAP:
		size = sizeof(uint64_t);
		break;
	case REDUCE_MTYPE_CHUNK_MAP:
		size =  _reduce_vol_get_chunk_struct_size(params->chunk_size / params->backing_io_unit_size);
		break;
	default:
		break;
	}

	return size;
}

static inline uint64_t
_meta_get_elem_count(struct spdk_reduce_vol_params *params, enum spdk_reduce_meta_type mtype,
		     uint64_t backing_dev_size)
{
	uint64_t count = 0;

	switch (mtype) {
	case REDUCE_MTYPE_SB:
	case REDUCE_MTYPE_PATH:
		count = 1;
		break;
	case REDUCE_MTYPE_LOGICAL_MAP:
	case REDUCE_MTYPE_CHUNK_MAP:
		count = backing_dev_size / params->chunk_size;
		break;
	default:
		assert(0);
	}

	return count;
}

/* Calc the metaregion offset and length according backing dev size,
 * then datasize = backing dev size - metasize.
 * The data region that 'logical_map' and 'chunkmap' can represent is a little more than the true data region.
 * It is neccessary to set the bits on the allocated bitarray of allocated_backing_io_units which represent meta region.
 */
static void
_init_builtin_meta_region_params(struct spdk_reduce_vol_params *params, uint64_t backing_dev_size)
{
	/* tmpoff unit is REDUCE_META_BLKSZ */
	uint64_t tmpoff = 0;
	uint32_t mtype;
	struct spdk_reduce_meta_desc *meta_region_desc = params->meta_region_desc;

	for (mtype = REDUCE_MTYPE_SB; mtype < REDUCE_MTYPE_NR; mtype++) {
		/* Although size_per_elem and elems_per_mblk could calc at runtime,
		 * calc it at init time could avoid calc it every time when edit a new mblk,
		 * especially at I/O path.
		 */
		meta_region_desc[mtype].size_per_elem = _meta_get_elem_struct_size(params, mtype);
		meta_region_desc[mtype].elems_per_mblk = REDUCE_META_BLKSZ / meta_region_desc[mtype].size_per_elem;
		meta_region_desc[mtype].offset = tmpoff;
		meta_region_desc[mtype].length = spdk_divide_round_up(_meta_get_elem_count(params, mtype,
						 backing_dev_size), meta_region_desc[mtype].elems_per_mblk);
		tmpoff += meta_region_desc[mtype].length;
	}

	/* data */
	params->data_region_off = tmpoff * REDUCE_META_BLKSZ;
	params->vol_size = _meta_builtin_get_vol_size(backing_dev_size, params);
}

static int
_alloc_meta_buf(struct spdk_reduce_vol *vol, struct reduce_init_load_ctx *init_ctx)
{
	struct reduce_mblk_init_ctx *mblk_init_ctx = &init_ctx->mblk_init_ctx;

	mblk_init_ctx->buf_len = SPDK_ALIGN_FLOOR(REDUCE_META_INIT_BUF_SIZE, REDUCE_META_BLKSZ);
	mblk_init_ctx->meta_init_buf = spdk_malloc(mblk_init_ctx->buf_len,
				       64, NULL, SPDK_ENV_LCORE_ID_ANY,
				       SPDK_MALLOC_DMA);
	if (mblk_init_ctx->meta_init_buf == NULL) {
		return -ENOMEM;
	}
	memset(mblk_init_ctx->meta_init_buf, 0xff, mblk_init_ctx->buf_len);

	return 0;
}

void
spdk_reduce_vol_init(struct spdk_reduce_vol_params *params,
		     struct spdk_reduce_backing_dev *backing_dev,
		     const char *pm_file_dir,
		     spdk_reduce_vol_op_with_handle_complete cb_fn, void *cb_arg)
{
	struct spdk_reduce_vol *vol;
	struct reduce_init_load_ctx *init_ctx;
	struct spdk_reduce_backing_io *backing_io;
	uint64_t backing_dev_size;
	size_t mapped_len;
	int dir_len = 0, max_dir_len, rc;

	rc = _validate_vol_params(params);
	if (rc != 0) {
		SPDK_ERRLOG("invalid vol params\n");
		cb_fn(cb_arg, NULL, rc);
		return;
	}

	backing_dev_size = backing_dev->blockcnt * backing_dev->blocklen;
	params->vol_size = _get_vol_size(params->chunk_size, backing_dev_size);
	if (params->vol_size == 0) {
		SPDK_ERRLOG("backing device is too small\n");
		cb_fn(cb_arg, NULL, -EINVAL);
		return;
	}

	if (backing_dev->submit_backing_io == NULL) {
		SPDK_ERRLOG("backing_dev function pointer not specified\n");
		cb_fn(cb_arg, NULL, -EINVAL);
		return;
	}

	if (spdk_uuid_is_null(&params->uuid)) {
		spdk_uuid_generate(&params->uuid);
	}

	if (!_check_meta_builtin(pm_file_dir)) {
		/* Use pmem to host metadata */
		params->meta_builtin = false;
		/* We need to append a path separator and the UUID to the supplied
		 * path.
		 */
		max_dir_len = REDUCE_PATH_MAX - SPDK_UUID_STRING_LEN - 1;
		dir_len = strnlen(pm_file_dir, max_dir_len);
		/* Strip trailing slash if the user provided one - we will add it back
		 * later when appending the filename.
		 */
		if (pm_file_dir[dir_len - 1] == '/') {
			dir_len--;
		}
		if (dir_len == max_dir_len) {
			SPDK_ERRLOG("pm_file_dir (%s) too long\n", pm_file_dir);
			cb_fn(cb_arg, NULL, -EINVAL);
			return;
		}
	} else {
		/* Use backing dev space to hold metadata */
		params->meta_builtin = true;
	}


	vol = calloc(1, sizeof(*vol));
	if (vol == NULL) {
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	TAILQ_INIT(&vol->free_requests);
	RB_INIT(&vol->executing_requests);
	TAILQ_INIT(&vol->queued_requests);
	queue_init(&vol->free_chunks_queue);
	queue_init(&vol->free_backing_blocks_queue);
	TAILQ_INIT(&vol->metablocks_free);
	TAILQ_INIT(&vol->metablocks_lru);
	RB_INIT(&vol->metablocks_caching);
	TAILQ_INIT(&vol->free_meta_request);

	vol->backing_super = spdk_zmalloc(sizeof(*vol->backing_super), 0, NULL,
					  SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
	if (vol->backing_super == NULL) {
		cb_fn(cb_arg, NULL, -ENOMEM);
		_init_load_cleanup(vol, NULL);
		return;
	}

	init_ctx = calloc(1, sizeof(*init_ctx));
	if (init_ctx == NULL) {
		cb_fn(cb_arg, NULL, -ENOMEM);
		_init_load_cleanup(vol, NULL);
		return;
	}

	backing_io = calloc(1, sizeof(*backing_io) + backing_dev->user_ctx_size);
	if (backing_io == NULL) {
		cb_fn(cb_arg, NULL, -ENOMEM);
		_init_load_cleanup(vol, init_ctx);
		return;
	}
	init_ctx->backing_io = backing_io;

	init_ctx->path = spdk_zmalloc(REDUCE_PATH_MAX, 0, NULL,
				      SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
	if (init_ctx->path == NULL) {
		cb_fn(cb_arg, NULL, -ENOMEM);
		_init_load_cleanup(vol, init_ctx);
		return;
	}

	vol->backing_io_units_per_chunk = params->chunk_size / params->backing_io_unit_size;
	vol->logical_blocks_per_chunk = params->chunk_size / params->logical_block_size;
	vol->backing_lba_per_io_unit = params->backing_io_unit_size / backing_dev->blocklen;
	vol->backing_dev = backing_dev;

	/* meta in pmem: init metadata in pmemdir.
	   meta in backing_bdev: split total region to meta region and data region.
	 */
	if (params->meta_builtin == false) {
		assert(dir_len != 0);
		memcpy(vol->pm_file.path, pm_file_dir, dir_len);
		vol->pm_file.path[dir_len] = '/';
		spdk_uuid_fmt_lower(&vol->pm_file.path[dir_len + 1], SPDK_UUID_STRING_LEN,
				    &params->uuid);
		vol->pm_file.size = _get_pm_file_size(params);
		vol->pm_file.pm_buf = pmem_map_file(vol->pm_file.path, vol->pm_file.size,
						    PMEM_FILE_CREATE | PMEM_FILE_EXCL, 0600,
						    &mapped_len, &vol->pm_file.pm_is_pmem);
		if (vol->pm_file.pm_buf == NULL) {
			SPDK_ERRLOG("could not pmem_map_file(%s): %s\n",
				    vol->pm_file.path, strerror(errno));
			cb_fn(cb_arg, NULL, -errno);
			_init_load_cleanup(vol, init_ctx);
			return;
		}

		if (vol->pm_file.size != mapped_len) {
			SPDK_ERRLOG("could not map entire pmem file (size=%" PRIu64 " mapped=%" PRIu64 ")\n",
				    vol->pm_file.size, mapped_len);
			cb_fn(cb_arg, NULL, -ENOMEM);
			_init_load_cleanup(vol, init_ctx);
			return;
		}

		memcpy(&vol->params, params, sizeof(*params));
		_initialize_vol_pm_pointers(vol);
	} else {
		/* metadata region on backingbdev
		|0	|4096	|8192...	|
		|sb	|mdpath	|logical_map...	|chunk_map...
		the order of initailization: pmpath, normalmeta(logical_map, chunk_map), sb
		 */
		_init_builtin_meta_region_params(params, backing_dev_size);
		memcpy(&vol->params, params, sizeof(*params));
	}


	memcpy(vol->backing_super->signature, SPDK_REDUCE_SIGNATURE,
	       sizeof(vol->backing_super->signature));
	memcpy(&vol->backing_super->params, params, sizeof(*params));

	if (params->meta_builtin == false) {
		memcpy(vol->pm_super, vol->backing_super, sizeof(*vol->backing_super));
		/* Writing 0xFF's is equivalent of filling it all with SPDK_EMPTY_MAP_ENTRY.
		 * Note that this writes 0xFF to not just the logical map but the chunk maps as well.
		 */
		memset(vol->pm_logical_map, 0xFF, vol->pm_file.size - sizeof(*vol->backing_super));
		_reduce_persist(vol, vol->pm_file.pm_buf, vol->pm_file.size);
	}

	rc = _allocate_bit_arrays(vol);
	if (rc != 0) {
		cb_fn(cb_arg, NULL, rc);
		_init_load_cleanup(vol, init_ctx);
		return;
	}

	init_ctx->vol = vol;
	init_ctx->cb_fn = cb_fn;
	init_ctx->cb_arg = cb_arg;

	if (params->meta_builtin) {
		memcpy(init_ctx->path, REDUCE_META_BUILTIN, sizeof(REDUCE_META_BUILTIN));
		rc = _alloc_meta_buf(vol, init_ctx);
		if (rc != 0) {
			cb_fn(cb_arg, NULL, rc);
			_init_load_cleanup(vol, init_ctx);
			return;
		}
		_init_builtin_meta_region_ctx_init(init_ctx);
		_init_builtin_meta_region(init_ctx, 0);
	} else {
		memcpy(init_ctx->path, vol->pm_file.path, REDUCE_PATH_MAX);
		_init_builtin_meta_region_cpl(init_ctx, 0);
	}
}

static void destroy_load_cb(void *cb_arg, struct spdk_reduce_vol *vol, int reduce_errno);


static struct reduce_meta_request *
_get_init_meta_req(struct spdk_reduce_vol *vol, bool is_write,
		   enum spdk_reduce_meta_type mtype, uint32_t mblk_sn, uint32_t elemsn_on_mblk,
		   spdk_reduce_dev_cpl cb_fn)
{
	struct reduce_meta_request *meta_req;
	meta_req = TAILQ_FIRST(&vol->free_meta_request);
	if (meta_req == NULL) {
		/* should not happend, because we have allocated enough metateq. 2 metareq for each vol req */
		assert(0);
		return meta_req;
	}
	TAILQ_REMOVE(&vol->free_meta_request, meta_req, tailq);
	meta_req->vol = vol;
	meta_req->is_write = is_write;
	meta_req->supply_read = is_write ? 1 : 0;
	meta_req->mtype = mtype;
	meta_req->mblk_sn = mblk_sn;
	meta_req->elem_sn_on_mblk = elemsn_on_mblk;
	meta_req->cb_args.cb_fn = cb_fn;
	meta_req->cb_args.cb_arg = meta_req;

	return meta_req;
}

static void _load_allocated_backing_io_unit(struct reduce_init_load_ctx *load_ctx, uint32_t mblksn);

/* restore the vol->allocated_backing_io_units by meta blocks */
static void
_load_allocated_backing_io_unit_cb(void *cb_arg, int reduce_errno)
{
	struct reduce_meta_request *meta_req = cb_arg;
	struct reduce_init_load_ctx *load_ctx = meta_req->restore_ctx.load_ctx.load_ctx;
	struct spdk_reduce_vol *vol = meta_req->vol;
	struct spdk_reduce_meta_desc *mdesc = &vol->params.meta_region_desc[REDUCE_MTYPE_CHUNK_MAP];
	void *chunk_array;
	struct spdk_reduce_chunk_map *chunk = NULL;
	/* chunk_off chunk_count describes the location of the chunkmap contained in this metadata block
	 * among all chunkmaps in the reduce volume. Just like offset and length.
	 */
	uint32_t chunk_off, chunk_count, chunk_size, chunk_idx, next_mblksn, chunkmap_idx, i, io_unit;
	uint64_t io_unit_index;

	if (reduce_errno != 0) {
		goto error;
	}

	chunk_size = mdesc->size_per_elem;
	chunk_off = (meta_req->mblk_sn - mdesc->offset) * mdesc->elems_per_mblk;
	chunk_count = mdesc->elems_per_mblk;

	chunk_array = _reduce_get_meta_elem_addr(meta_req);
	for (i = 0; i < chunk_count; i++) {
		chunk_idx = chunk_off + i;
		/* one chunkmap metablock contains lots chunmap, skip unused chunks */
		if (0 == spdk_bit_array_get(vol->allocated_chunk_maps, chunk_idx)) {
			continue;
		}
		chunk = (struct spdk_reduce_chunk_map *)(chunk_array + i * chunk_size);
		for (io_unit = 0; io_unit < vol->backing_io_units_per_chunk; io_unit++) {
			io_unit_index = chunk->io_unit_index[io_unit];
			if (io_unit_index == REDUCE_EMPTY_MAP_ENTRY) {
				continue;
			}
			spdk_bit_array_set(vol->allocated_backing_io_units, io_unit_index);
		}
	}
	/* we only read the chunkmap mblks which contain used chunks */
	assert(chunk);
	_reduce_metablock_cache_read_done_update(meta_req, reduce_errno);

	chunkmap_idx = spdk_bit_array_find_first_set(vol->allocated_chunk_maps, chunk_off + chunk_count);
	if (chunkmap_idx == UINT32_MAX) {
		SPDK_NOTICELOG("vol:%p io unit bitarray load finish\n", vol);
		load_ctx->cb_fn(load_ctx->cb_arg, vol, 0);
		_init_load_cleanup(NULL, load_ctx);
		return;
	}

	/* load next chunkmap mblks */
	next_mblksn = _meta_get_mblksn(&vol->params, REDUCE_MTYPE_CHUNK_MAP, chunkmap_idx);
	_load_allocated_backing_io_unit(load_ctx, next_mblksn);
	return;

error:
	load_ctx->cb_fn(load_ctx->cb_arg, NULL, reduce_errno);
	_init_load_cleanup(vol, load_ctx);
}

static void
_load_allocated_backing_io_unit(struct reduce_init_load_ctx *load_ctx, uint32_t mblksn)
{
	struct spdk_reduce_vol *vol = load_ctx->vol;
	struct reduce_meta_request *meta_req;
	uint32_t next_mblksn, chunkmap_idx;

	if (mblksn == UINT32_MAX) {
		chunkmap_idx = spdk_bit_array_find_first_set(vol->allocated_chunk_maps, 0);
		if (chunkmap_idx == UINT32_MAX) {
			/* means the compress volume is empty */
			SPDK_NOTICELOG("vol:%p backing io units don't need load\n", vol);
			load_ctx->cb_fn(load_ctx->cb_arg, vol, 0);
			_init_load_cleanup(NULL, load_ctx);
			return;
		}
		next_mblksn = _meta_get_mblksn(&vol->params, REDUCE_MTYPE_CHUNK_MAP, chunkmap_idx);
	} else {
		next_mblksn = mblksn;
	}

	meta_req = _get_init_meta_req(vol, false, REDUCE_MTYPE_CHUNK_MAP,
				      next_mblksn, 0,
				      _load_allocated_backing_io_unit_cb);
	meta_req->restore_ctx.load_ctx.load_ctx = load_ctx;
	_reduce_metablock_cache_read(meta_req);
}

static void _load_allocated_chunk_map_by_md(struct reduce_init_load_ctx *load_ctx, uint32_t mblksn);

/* restore the vol->allocated_chunk_maps by meta blocks */
static void
_load_allocated_chunk_map_cb(void *cb_arg, int reduce_errno)
{
	struct reduce_meta_request *meta_req = cb_arg;
	struct reduce_init_load_ctx *load_ctx = meta_req->restore_ctx.load_ctx.load_ctx;
	struct spdk_reduce_vol *vol = meta_req->vol;
	struct spdk_reduce_meta_desc *mdesc = &vol->params.meta_region_desc[REDUCE_MTYPE_LOGICAL_MAP];
	uint64_t *logical_map_array, logical_map;
	uint32_t next_mblksn;

	if (reduce_errno != 0) {
		goto error;
	}

	logical_map_array = _reduce_get_meta_elem_addr(meta_req);
	for (int i = 0; i < mdesc->elems_per_mblk; i++) {
		logical_map = logical_map_array[i];
		if (logical_map == REDUCE_EMPTY_MAP_ENTRY) {
			continue;
		}
		spdk_bit_array_set(vol->allocated_chunk_maps, logical_map);
	}

	next_mblksn = meta_req->mblk_sn + 1;
	_reduce_metablock_cache_read_done_update(meta_req, reduce_errno);

	if (next_mblksn == mdesc->offset + mdesc->length) {
		SPDK_NOTICELOG("vol:%p logical map bitarray load finish\n", vol);
		_load_allocated_backing_io_unit(load_ctx, UINT32_MAX);
		return;
	}

	_load_allocated_chunk_map_by_md(load_ctx, next_mblksn);
	return;

error:
	load_ctx->cb_fn(load_ctx->cb_arg, NULL, reduce_errno);
	_init_load_cleanup(vol, load_ctx);
}

static void
_load_allocated_chunk_map_by_md(struct reduce_init_load_ctx *load_ctx, uint32_t mblksn)
{
	struct spdk_reduce_vol *vol = load_ctx->vol;
	struct spdk_reduce_meta_desc *md_desc = &vol->params.meta_region_desc[REDUCE_MTYPE_LOGICAL_MAP];
	struct reduce_meta_request *meta_req;
	uint32_t next_mblksn = mblksn == UINT32_MAX ? md_desc->offset : mblksn;

	meta_req = _get_init_meta_req(vol, false, REDUCE_MTYPE_LOGICAL_MAP,
				      next_mblksn, 0,
				      _load_allocated_chunk_map_cb);
	meta_req->restore_ctx.load_ctx.load_ctx = load_ctx;
	_reduce_metablock_cache_read(meta_req);
}

static void
_load_read_super_and_path_cpl(void *cb_arg, int reduce_errno)
{
	struct reduce_init_load_ctx *load_ctx = cb_arg;
	struct spdk_reduce_vol *vol = load_ctx->vol;
	uint64_t backing_dev_size;
	uint64_t i, num_chunks, logical_map_index;
	struct spdk_reduce_chunk_map *chunk;
	size_t mapped_len;
	uint32_t j;
	int rc;

	if (reduce_errno != 0) {
		rc = reduce_errno;
		goto error;
	}

	rc = _alloc_zero_buff();
	if (rc) {
		goto error;
	}

	if (memcmp(vol->backing_super->signature,
		   SPDK_REDUCE_SIGNATURE,
		   sizeof(vol->backing_super->signature)) != 0) {
		/* This backing device isn't a libreduce backing device. */
		rc = -EILSEQ;
		goto error;
	}

	/* If the cb_fn is destroy_load_cb, it means we are wanting to destroy this compress bdev.
	 *  So don't bother getting the volume ready to use - invoke the callback immediately
	 *  so destroy_load_cb can delete the metadata off of the block device and delete the
	 *  persistent memory file if it exists.
	 */
	memcpy(vol->pm_file.path, load_ctx->path, sizeof(vol->pm_file.path));
	if (load_ctx->cb_fn == (*destroy_load_cb)) {
		load_ctx->cb_fn(load_ctx->cb_arg, vol, 0);
		_init_load_cleanup(NULL, load_ctx);
		return;
	}

	assert(vol->backing_super->params.meta_builtin == _check_meta_builtin(vol->pm_file.path));

	memcpy(&vol->params, &vol->backing_super->params, sizeof(vol->params));
	vol->backing_io_units_per_chunk = vol->params.chunk_size / vol->params.backing_io_unit_size;
	vol->logical_blocks_per_chunk = vol->params.chunk_size / vol->params.logical_block_size;
	vol->backing_lba_per_io_unit = vol->params.backing_io_unit_size / vol->backing_dev->blocklen;

	rc = _allocate_bit_arrays(vol);
	if (rc != 0) {
		goto error;
	}

	backing_dev_size = vol->backing_dev->blockcnt * vol->backing_dev->blocklen;
	if (_get_vol_size(vol->params.chunk_size, backing_dev_size) < vol->params.vol_size) {
		SPDK_ERRLOG("backing device size %" PRIi64 " smaller than expected\n",
			    backing_dev_size);
		rc = -EILSEQ;
		goto error;
	}

	rc = _allocate_vol_requests(vol);
	if (rc != 0) {
		goto error;
	}

	if (vol->params.meta_builtin) {
		rc = _allocate_vol_metablock_and_metaio(load_ctx->vol);
		if (rc != 0) {
			goto error;
		}
		_load_allocated_chunk_map_by_md(load_ctx, UINT32_MAX);
		return;
	}

	vol->pm_file.size = _get_pm_file_size(&vol->params);
	vol->pm_file.pm_buf = pmem_map_file(vol->pm_file.path, 0, 0, 0, &mapped_len,
					    &vol->pm_file.pm_is_pmem);
	if (vol->pm_file.pm_buf == NULL) {
		SPDK_ERRLOG("could not pmem_map_file(%s): %s\n", vol->pm_file.path, strerror(errno));
		rc = -errno;
		goto error;
	}

	if (vol->pm_file.size != mapped_len) {
		SPDK_ERRLOG("could not map entire pmem file (size=%" PRIu64 " mapped=%" PRIu64 ")\n",
			    vol->pm_file.size, mapped_len);
		rc = -ENOMEM;
		goto error;
	}

	_initialize_vol_pm_pointers(vol);

	num_chunks = vol->params.vol_size / vol->params.chunk_size;
	for (i = 0; i < num_chunks; i++) {
		logical_map_index = vol->pm_logical_map[i];
		if (logical_map_index == REDUCE_EMPTY_MAP_ENTRY) {
			continue;
		}
		spdk_bit_array_set(vol->allocated_chunk_maps, logical_map_index);
		chunk = _reduce_vol_get_chunk_map(vol, logical_map_index);
		for (j = 0; j < vol->backing_io_units_per_chunk; j++) {
			if (chunk->io_unit_index[j] != REDUCE_EMPTY_MAP_ENTRY) {
				spdk_bit_array_set(vol->allocated_backing_io_units, chunk->io_unit_index[j]);
				vol->info.allocated_io_units++;
			}
		}
	}

	load_ctx->cb_fn(load_ctx->cb_arg, vol, 0);
	/* Only clean up the ctx - the vol has been passed to the application
	 *  for use now that volume load was successful.
	 */
	_init_load_cleanup(NULL, load_ctx);
	return;

error:
	load_ctx->cb_fn(load_ctx->cb_arg, NULL, rc);
	_init_load_cleanup(vol, load_ctx);
}

void
spdk_reduce_vol_load(struct spdk_reduce_backing_dev *backing_dev,
		     spdk_reduce_vol_op_with_handle_complete cb_fn, void *cb_arg)
{
	struct spdk_reduce_vol *vol;
	struct reduce_init_load_ctx *load_ctx;
	struct spdk_reduce_backing_io *backing_io;

	if (backing_dev->submit_backing_io == NULL) {
		SPDK_ERRLOG("backing_dev function pointer not specified\n");
		cb_fn(cb_arg, NULL, -EINVAL);
		return;
	}

	vol = calloc(1, sizeof(*vol));
	if (vol == NULL) {
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	TAILQ_INIT(&vol->free_requests);
	RB_INIT(&vol->executing_requests);
	TAILQ_INIT(&vol->queued_requests);
	queue_init(&vol->free_chunks_queue);
	queue_init(&vol->free_backing_blocks_queue);
	TAILQ_INIT(&vol->metablocks_free);
	TAILQ_INIT(&vol->metablocks_lru);
	RB_INIT(&vol->metablocks_caching);
	TAILQ_INIT(&vol->free_meta_request);

	vol->backing_super = spdk_zmalloc(sizeof(*vol->backing_super), 64, NULL,
					  SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
	if (vol->backing_super == NULL) {
		_init_load_cleanup(vol, NULL);
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	vol->backing_dev = backing_dev;

	load_ctx = calloc(1, sizeof(*load_ctx));
	if (load_ctx == NULL) {
		_init_load_cleanup(vol, NULL);
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	backing_io = calloc(1, sizeof(*backing_io) + backing_dev->user_ctx_size);
	if (backing_io == NULL) {
		_init_load_cleanup(vol, load_ctx);
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	load_ctx->backing_io = backing_io;

	load_ctx->path = spdk_zmalloc(REDUCE_PATH_MAX, 64, NULL,
				      SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
	if (load_ctx->path == NULL) {
		_init_load_cleanup(vol, load_ctx);
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	load_ctx->vol = vol;
	load_ctx->cb_fn = cb_fn;
	load_ctx->cb_arg = cb_arg;

	load_ctx->iov[0].iov_base = vol->backing_super;
	load_ctx->iov[0].iov_len = sizeof(*vol->backing_super);
	load_ctx->iov[1].iov_base = load_ctx->path;
	load_ctx->iov[1].iov_len = REDUCE_PATH_MAX;
	backing_io->dev = vol->backing_dev;
	backing_io->iov = load_ctx->iov;
	backing_io->iovcnt = LOAD_IOV_COUNT;
	backing_io->lba = 0;
	backing_io->lba_count = (sizeof(*vol->backing_super) + REDUCE_PATH_MAX) /
				vol->backing_dev->blocklen;
	backing_io->backing_cb_args = &load_ctx->backing_cb_args;
	backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_READ;

	load_ctx->backing_cb_args.cb_fn = _load_read_super_and_path_cpl;
	load_ctx->backing_cb_args.cb_arg = load_ctx;
	vol->backing_dev->submit_backing_io(backing_io);
}

void
spdk_reduce_vol_unload(struct spdk_reduce_vol *vol,
		       spdk_reduce_vol_op_complete cb_fn, void *cb_arg)
{
	if (vol == NULL) {
		/* This indicates a programming error. */
		assert(false);
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	if (--g_vol_count == 0) {
		spdk_free(g_zero_buf);
	}
	assert(g_vol_count >= 0);
	_init_load_cleanup(vol, NULL);
	cb_fn(cb_arg, 0);
}

struct reduce_destroy_ctx {
	spdk_reduce_vol_op_complete		cb_fn;
	void					*cb_arg;
	struct spdk_reduce_vol			*vol;
	struct spdk_reduce_vol_superblock	*super;
	struct iovec				iov;
	struct spdk_reduce_vol_cb_args		backing_cb_args;
	int					reduce_errno;
	char					pm_path[REDUCE_PATH_MAX];
	struct spdk_reduce_backing_io           *backing_io;
};

static void
destroy_unload_cpl(void *cb_arg, int reduce_errno)
{
	struct reduce_destroy_ctx *destroy_ctx = cb_arg;

	if (destroy_ctx->reduce_errno == 0) {
		if (!_check_meta_builtin(destroy_ctx->pm_path) && unlink(destroy_ctx->pm_path)) {
			SPDK_ERRLOG("%s could not be unlinked: %s\n",
				    destroy_ctx->pm_path, strerror(errno));
		}
	}

	/* Even if the unload somehow failed, we still pass the destroy_ctx
	 * reduce_errno since that indicates whether or not the volume was
	 * actually destroyed.
	 */
	destroy_ctx->cb_fn(destroy_ctx->cb_arg, destroy_ctx->reduce_errno);
	spdk_free(destroy_ctx->super);
	free(destroy_ctx->backing_io);
	free(destroy_ctx);
}

static void
_destroy_zero_super_cpl(void *cb_arg, int reduce_errno)
{
	struct reduce_destroy_ctx *destroy_ctx = cb_arg;
	struct spdk_reduce_vol *vol = destroy_ctx->vol;

	destroy_ctx->reduce_errno = reduce_errno;
	spdk_reduce_vol_unload(vol, destroy_unload_cpl, destroy_ctx);
}

static void
destroy_load_cb(void *cb_arg, struct spdk_reduce_vol *vol, int reduce_errno)
{
	struct reduce_destroy_ctx *destroy_ctx = cb_arg;
	struct spdk_reduce_backing_io *backing_io = destroy_ctx->backing_io;

	if (reduce_errno != 0) {
		destroy_ctx->cb_fn(destroy_ctx->cb_arg, reduce_errno);
		spdk_free(destroy_ctx->super);
		free(destroy_ctx);
		return;
	}

	destroy_ctx->vol = vol;
	memcpy(destroy_ctx->pm_path, vol->pm_file.path, sizeof(destroy_ctx->pm_path));
	destroy_ctx->iov.iov_base = destroy_ctx->super;
	destroy_ctx->iov.iov_len = sizeof(*destroy_ctx->super);
	destroy_ctx->backing_cb_args.cb_fn = _destroy_zero_super_cpl;
	destroy_ctx->backing_cb_args.cb_arg = destroy_ctx;

	backing_io->dev = vol->backing_dev;
	backing_io->iov = &destroy_ctx->iov;
	backing_io->iovcnt = 1;
	backing_io->lba = 0;
	backing_io->lba_count = sizeof(*destroy_ctx->super) / vol->backing_dev->blocklen;
	backing_io->backing_cb_args = &destroy_ctx->backing_cb_args;
	backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_WRITE;

	vol->backing_dev->submit_backing_io(backing_io);
}

void
spdk_reduce_vol_destroy(struct spdk_reduce_backing_dev *backing_dev,
			spdk_reduce_vol_op_complete cb_fn, void *cb_arg)
{
	struct reduce_destroy_ctx *destroy_ctx;
	struct spdk_reduce_backing_io *backing_io;

	destroy_ctx = calloc(1, sizeof(*destroy_ctx));
	if (destroy_ctx == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	backing_io = calloc(1, sizeof(*backing_io) + backing_dev->user_ctx_size);
	if (backing_io == NULL) {
		free(destroy_ctx);
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	destroy_ctx->backing_io = backing_io;

	destroy_ctx->super = spdk_zmalloc(sizeof(*destroy_ctx->super), 64, NULL,
					  SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
	if (destroy_ctx->super == NULL) {
		free(destroy_ctx);
		free(backing_io);
		cb_fn(cb_arg, -ENOMEM);
		return;
	}
	destroy_ctx->cb_fn = cb_fn;
	destroy_ctx->cb_arg = cb_arg;
	spdk_reduce_vol_load(backing_dev, destroy_load_cb, destroy_ctx);
}

static bool
_request_spans_chunk_boundary(struct spdk_reduce_vol *vol, uint64_t offset, uint64_t length)
{
	uint64_t start_chunk, end_chunk;

	start_chunk = offset / vol->logical_blocks_per_chunk;
	end_chunk = (offset + length - 1) / vol->logical_blocks_per_chunk;

	return (start_chunk != end_chunk);
}

static void _start_unmap_request_full_chunk(void *ctx);

static void
_reduce_vol_complete_req(struct spdk_reduce_vol_request *req, int reduce_errno)
{
	struct spdk_reduce_vol_request *next_req;
	struct spdk_reduce_vol *vol = req->vol;

	req->cb_fn(req->cb_arg, reduce_errno);
	RB_REMOVE(executing_req_tree, &vol->executing_requests, req);

	TAILQ_FOREACH(next_req, &vol->queued_requests, tailq) {
		if (next_req->logical_map_index == req->logical_map_index) {
			TAILQ_REMOVE(&vol->queued_requests, next_req, tailq);
			if (next_req->type == REDUCE_IO_READV) {
				_start_readv_request(next_req);
			} else if (next_req->type == REDUCE_IO_WRITEV) {
				_start_writev_request(next_req);
			} else {
				assert(next_req->type == REDUCE_IO_UNMAP);
				_start_unmap_request_full_chunk(next_req);
			}
			break;
		}
	}

	TAILQ_INSERT_HEAD(&vol->free_requests, req, tailq);
}

static void
_reduce_vol_reset_chunk(struct spdk_reduce_vol *vol, uint64_t chunk_map_index,
			struct spdk_reduce_chunk_map *chunkaddr)
{
	struct spdk_reduce_chunk_map *chunk;
	uint64_t index;
	bool success;
	uint32_t i;

	assert(chunk_map_index != REDUCE_EMPTY_MAP_ENTRY);
	if (chunkaddr == NULL) {
		chunk = _reduce_vol_get_chunk_map(vol, chunk_map_index);
	} else {
		chunk = chunkaddr;
	}
	SPDK_INFOLOG(reduce, "release %lu. %lu %lu %lu %lu\n",
		     chunk_map_index, chunk->io_unit_index[0], chunk->io_unit_index[1], chunk->io_unit_index[2],
		     chunk->io_unit_index[3]);
	for (i = 0; i < vol->backing_io_units_per_chunk; i++) {
		index = chunk->io_unit_index[i];
		if (index == REDUCE_EMPTY_MAP_ENTRY) {
			break;
		}
		assert(spdk_bit_array_get(vol->allocated_backing_io_units,
					  index) == true);
		spdk_bit_array_clear(vol->allocated_backing_io_units, index);
		vol->info.allocated_io_units--;
		success = queue_enqueue(&vol->free_backing_blocks_queue, index);
		if (!success && index < vol->find_block_offset) {
			vol->find_block_offset = index;
		}
		chunk->io_unit_index[i] = REDUCE_EMPTY_MAP_ENTRY;
	}
	success = queue_enqueue(&vol->free_chunks_queue, chunk_map_index);
	if (!success && chunk_map_index < vol->find_chunk_offset) {
		vol->find_chunk_offset = chunk_map_index;
	}
	spdk_bit_array_clear(vol->allocated_chunk_maps, chunk_map_index);
}

static inline void
convert_supply_read_to_write(struct reduce_meta_request *meta_req)
{
	assert(meta_req->is_write && meta_req->supply_read);
	meta_req->supply_read = false;
}

static void
_write_done_mapinfo_update_error_process(struct reduce_meta_request *meta_req, int error)
{
	struct spdk_reduce_vol_request *req = meta_req->restore_ctx.req_ctx.req;
	char uuid_str[SPDK_UUID_STRING_LEN];

	/* update chunk map or logical fail, we need to release the bit from
	 * allocated_chunk_maps and allocated_backing_io_units where we want to write.
	 */
	_reduce_vol_reset_chunk(req->vol, req->chunk_map_index, req->chunk);
	spdk_uuid_fmt_lower(uuid_str, sizeof(uuid_str), spdk_reduce_vol_get_uuid(req->vol));
	SPDK_ERRLOG("%s write update new mapping error %d, release chunkmap idx: %lu, backing io unit: %lu %lu %lu %lu\n",
		    uuid_str, error, req->chunk_map_index,
		    req->chunk->io_unit_index[0], req->chunk->io_unit_index[1], req->chunk->io_unit_index[2],
		    req->chunk->io_unit_index[3]);
	req->reduce_errno = error;
	_reduce_vol_complete_req(req, req->reduce_errno);
	if (meta_req->supply_read || !meta_req->is_write) {
		_reduce_metablock_cache_read_done_update(meta_req, error);
	} else {
		_reduce_metablock_cache_write_done_update(meta_req, error);
	}
}

static void
_write_write_done_update_logical_map(void *_meta_req, int error)
{
	struct reduce_meta_request *meta_req = _meta_req;
	struct spdk_reduce_vol_request *req = meta_req->restore_ctx.req_ctx.req;
	uint64_t *logical_map;

	if (error != 0) {
		_write_done_mapinfo_update_error_process(meta_req, error);
		return;
	}

	if (meta_req->supply_read) {
		logical_map = (uint64_t *)_reduce_get_meta_elem_addr(meta_req);
		*logical_map = req->chunk_map_index;
		convert_supply_read_to_write(meta_req);
		_reduce_metablock_cache_read_done_update(meta_req, error);
	} else {
		_reduce_metablock_cache_write_done_update(meta_req, error);

		_reduce_vol_complete_req(req, error);
	}
}

static void
_write_write_done_update_chunk_map(void *_meta_req, int error)
{
	struct reduce_meta_request *meta_req = _meta_req;
	struct spdk_reduce_vol_request *req = meta_req->restore_ctx.req_ctx.req;
	struct spdk_reduce_vol *vol = req->vol;
	struct spdk_reduce_chunk_map *chunk;

	if (error != 0) {
		_write_done_mapinfo_update_error_process(meta_req, error);
		return;
	}

	if (meta_req->supply_read) {
		chunk = (struct spdk_reduce_chunk_map *)_reduce_get_meta_elem_addr(meta_req);
		memcpy(chunk, req->chunk, _reduce_vol_get_chunk_struct_size(vol->backing_io_units_per_chunk));
		convert_supply_read_to_write(meta_req);
		_reduce_metablock_cache_read_done_update(meta_req, error);
	} else {
		_reduce_metablock_cache_write_done_update(meta_req, error);
		meta_req = _get_init_meta_req(req->vol, true, REDUCE_MTYPE_LOGICAL_MAP,
					      _meta_get_mblksn(&vol->params, REDUCE_MTYPE_LOGICAL_MAP, req->logical_map_index),
					      _meta_get_elem_sn_on_mblk(&vol->params, REDUCE_MTYPE_LOGICAL_MAP, req->logical_map_index),
					      _write_write_done_update_logical_map);
		meta_req->restore_ctx.req_ctx.req = req;
		_reduce_metablock_cache_read(meta_req);
	}
}

static void
_write_write_done_clear_old_chunkmap(void *_meta_req, int error)
{
	struct reduce_meta_request *meta_req = _meta_req;
	struct spdk_reduce_vol_request *req = meta_req->restore_ctx.req_ctx.req;
	struct spdk_reduce_vol *vol = req->vol;
	struct spdk_reduce_chunk_map *chunk_addr;

	if (error != 0) {
		/* If clear the old chunkmap meta fail, we don't consider it will success when update new chunk map.
		 * There is no necessary to continue the rest of process.
		 * We also don't release old chunkmap and old logical map to protect the data when we read chunkmap failed.
		 * So just return error.
		 */
		_write_done_mapinfo_update_error_process(meta_req, error);
		return;
	}

	/* read the meta elem */
	if (meta_req->supply_read) {
		chunk_addr = (struct spdk_reduce_chunk_map *)_reduce_get_meta_elem_addr(meta_req);
		_reduce_vol_reset_chunk(req->vol, req->read_chunk_map_index, chunk_addr);

		/* meta data write backing */
		convert_supply_read_to_write(meta_req);
		_reduce_metablock_cache_read_done_update(meta_req, error);
	} else {
		_reduce_metablock_cache_write_done_update(meta_req, error);

		/* set the new chunk map */
		meta_req = _get_init_meta_req(req->vol, true, REDUCE_MTYPE_CHUNK_MAP,
					      _meta_get_mblksn(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
					      _meta_get_elem_sn_on_mblk(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
					      _write_write_done_update_chunk_map);
		meta_req->restore_ctx.req_ctx.req = req;
		_reduce_metablock_cache_read(meta_req);

		return;
	}
}

static void
_write_write_done(void *_req, int reduce_errno)
{
	struct spdk_reduce_vol_request *req = _req;
	struct spdk_reduce_vol *vol = req->vol;
	uint64_t old_chunk_map_index;
	struct reduce_meta_request *meta_req;

	if (reduce_errno != 0) {
		req->reduce_errno = reduce_errno;
	}

	assert(req->num_backing_ops > 0);
	if (--req->num_backing_ops > 0) {
		return;
	}

	if (req->reduce_errno != 0) {
		if (!vol->params.meta_builtin) {
			_reduce_vol_reset_chunk(vol, req->chunk_map_index, NULL);
		}
		/* for meta_builtin == true, we don't need handle the case, because we haven't change the metadata yet */
		_reduce_vol_complete_req(req, req->reduce_errno);
		return;
	}

	if (vol->params.meta_builtin) {
		if (req->read_chunk_map_index != REDUCE_EMPTY_MAP_ENTRY) {
			meta_req = _get_init_meta_req(req->vol, true, REDUCE_MTYPE_CHUNK_MAP,
						      _meta_get_mblksn(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->read_chunk_map_index),
						      _meta_get_elem_sn_on_mblk(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->read_chunk_map_index),
						      _write_write_done_clear_old_chunkmap);
			meta_req->restore_ctx.req_ctx.req = req;
			_reduce_metablock_cache_read(meta_req);
			return;
		} else {
			/* set the new chunk map */
			meta_req = _get_init_meta_req(req->vol, true, REDUCE_MTYPE_CHUNK_MAP,
						      _meta_get_mblksn(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
						      _meta_get_elem_sn_on_mblk(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
						      _write_write_done_update_chunk_map);
			meta_req->restore_ctx.req_ctx.req = req;
			_reduce_metablock_cache_read(meta_req);
			return;
		}
	}

	old_chunk_map_index = vol->pm_logical_map[req->logical_map_index];
	if (old_chunk_map_index != REDUCE_EMPTY_MAP_ENTRY) {
		_reduce_vol_reset_chunk(vol, old_chunk_map_index, NULL);
	}

	/*
	 * We don't need to persist the clearing of the old chunk map here.  The old chunk map
	 * becomes invalid after we update the logical map, since the old chunk map will no
	 * longer have a reference to it in the logical map.
	 */

	/* Persist the new chunk map.  This must be persisted before we update the logical map. */
	_reduce_persist(vol, req->chunk,
			_reduce_vol_get_chunk_struct_size(vol->backing_io_units_per_chunk));

	vol->pm_logical_map[req->logical_map_index] = req->chunk_map_index;

	_reduce_persist(vol, &vol->pm_logical_map[req->logical_map_index], sizeof(uint64_t));

	_reduce_vol_complete_req(req, 0);
}

static struct spdk_reduce_backing_io *
_reduce_vol_req_get_backing_io(struct spdk_reduce_vol_request *req, uint32_t index)
{
	struct spdk_reduce_backing_dev *backing_dev = req->vol->backing_dev;
	struct spdk_reduce_backing_io *backing_io;

	backing_io = (struct spdk_reduce_backing_io *)((uint8_t *)req->backing_io +
			(sizeof(*backing_io) + backing_dev->user_ctx_size) * index);

	return backing_io;

}

struct reduce_merged_io_desc {
	uint64_t io_unit_index;
	uint32_t num_io_units;
};

static void
_issue_backing_ops_without_merge(struct spdk_reduce_vol_request *req, struct spdk_reduce_vol *vol,
				 reduce_request_fn next_fn, bool is_write)
{
	struct iovec *iov;
	struct spdk_reduce_backing_io *backing_io;
	uint8_t *buf;
	uint32_t i;

	if (req->chunk_is_compressed) {
		iov = req->comp_buf_iov;
		buf = req->comp_buf;
	} else {
		iov = req->decomp_buf_iov;
		buf = req->decomp_buf;
	}

	req->num_backing_ops = req->num_io_units;
	req->backing_cb_args.cb_fn = next_fn;
	req->backing_cb_args.cb_arg = req;
	for (i = 0; i < req->num_io_units; i++) {
		backing_io = _reduce_vol_req_get_backing_io(req, i);
		iov[i].iov_base = buf + i * vol->params.backing_io_unit_size;
		iov[i].iov_len = vol->params.backing_io_unit_size;
		backing_io->dev  = vol->backing_dev;
		backing_io->iov = &iov[i];
		backing_io->iovcnt = 1;
		backing_io->lba = req->chunk->io_unit_index[i] * vol->backing_lba_per_io_unit;
		backing_io->lba_count = vol->backing_lba_per_io_unit;
		backing_io->backing_cb_args = &req->backing_cb_args;
		if (is_write) {
			backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_WRITE;
		} else {
			backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_READ;
		}
		vol->backing_dev->submit_backing_io(backing_io);
	}
}

static void
_issue_backing_ops(struct spdk_reduce_vol_request *req, struct spdk_reduce_vol *vol,
		   reduce_request_fn next_fn, bool is_write)
{
	struct iovec *iov;
	struct spdk_reduce_backing_io *backing_io;
	struct reduce_merged_io_desc merged_io_desc[4];
	uint8_t *buf;
	bool merge = false;
	uint32_t num_io = 0;
	uint32_t io_unit_counts = 0;
	uint32_t merged_io_idx = 0;
	uint32_t i;

	/* The merged_io_desc value is defined here to contain four elements,
	 * and the chunk size must be four times the maximum of the io unit.
	 * if chunk size is too big, don't merge IO.
	 */
	if (vol->backing_io_units_per_chunk > 4) {
		_issue_backing_ops_without_merge(req, vol, next_fn, is_write);
		return;
	}

	if (req->chunk_is_compressed) {
		iov = req->comp_buf_iov;
		buf = req->comp_buf;
	} else {
		iov = req->decomp_buf_iov;
		buf = req->decomp_buf;
	}

	for (i = 0; i < req->num_io_units; i++) {
		if (!merge) {
			merged_io_desc[merged_io_idx].io_unit_index = req->chunk->io_unit_index[i];
			merged_io_desc[merged_io_idx].num_io_units = 1;
			num_io++;
		}

		if (i + 1 == req->num_io_units) {
			break;
		}

		if (req->chunk->io_unit_index[i] + 1 == req->chunk->io_unit_index[i + 1]) {
			merged_io_desc[merged_io_idx].num_io_units += 1;
			merge = true;
			continue;
		}
		merge = false;
		merged_io_idx++;
	}

	req->num_backing_ops = num_io;
	req->backing_cb_args.cb_fn = next_fn;
	req->backing_cb_args.cb_arg = req;
	for (i = 0; i < num_io; i++) {
		backing_io = _reduce_vol_req_get_backing_io(req, i);
		iov[i].iov_base = buf + io_unit_counts * vol->params.backing_io_unit_size;
		iov[i].iov_len = vol->params.backing_io_unit_size * merged_io_desc[i].num_io_units;
		backing_io->dev  = vol->backing_dev;
		backing_io->iov = &iov[i];
		backing_io->iovcnt = 1;
		backing_io->lba = merged_io_desc[i].io_unit_index * vol->backing_lba_per_io_unit;
		backing_io->lba_count = vol->backing_lba_per_io_unit * merged_io_desc[i].num_io_units;
		backing_io->backing_cb_args = &req->backing_cb_args;
		if (is_write) {
			backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_WRITE;
		} else {
			backing_io->backing_io_type = SPDK_REDUCE_BACKING_IO_READ;
		}
		vol->backing_dev->submit_backing_io(backing_io);

		/* Collects the number of processed I/O. */
		io_unit_counts += merged_io_desc[i].num_io_units;
	}
}

static void
_reduce_vol_write_chunk(struct spdk_reduce_vol_request *req, reduce_request_fn next_fn,
			uint32_t compressed_size)
{
	struct spdk_reduce_vol *vol = req->vol;
	uint32_t i;
	uint64_t chunk_offset, remainder, free_index, total_len = 0;
	uint8_t *buf;
	bool success;
	int j;

	success = queue_dequeue(&vol->free_chunks_queue, &free_index);
	if (success) {
		req->chunk_map_index = free_index;
	} else {
		req->chunk_map_index = spdk_bit_array_find_first_clear(vol->allocated_chunk_maps,
				       vol->find_chunk_offset);
		vol->find_chunk_offset = req->chunk_map_index + 1;
	}

	/* TODO: fail if no chunk map found - but really this should not happen if we
	 * size the number of requests similarly to number of extra chunk maps
	 */
	assert(req->chunk_map_index != REDUCE_EMPTY_MAP_ENTRY);
	spdk_bit_array_set(vol->allocated_chunk_maps, req->chunk_map_index);

	if (vol->params.meta_builtin) {
		/* when use builtin meta, need the chunk memory from heap */
		req->chunk = req->prealloc_chunk;
		memset(req->chunk->io_unit_index, 0XFF, sizeof(uint64_t) * vol->backing_io_units_per_chunk);
	} else {
		req->chunk = _reduce_vol_get_chunk_map(vol, req->chunk_map_index);
	}
	req->num_io_units = spdk_divide_round_up(compressed_size,
			    vol->params.backing_io_unit_size);
	req->chunk_is_compressed = (req->num_io_units != vol->backing_io_units_per_chunk);
	req->chunk->compressed_size =
		req->chunk_is_compressed ? compressed_size : vol->params.chunk_size;

	/* if the chunk is uncompressed we need to copy the data from the host buffers. */
	if (req->chunk_is_compressed == false) {
		chunk_offset = req->offset % vol->logical_blocks_per_chunk;
		buf = req->decomp_buf;
		total_len = chunk_offset * vol->params.logical_block_size;

		/* zero any offset into chunk */
		if (req->rmw == false && chunk_offset) {
			memset(buf, 0, total_len);
		}
		buf += total_len;

		/* copy the data */
		for (j = 0; j < req->iovcnt; j++) {
			memcpy(buf, req->iov[j].iov_base, req->iov[j].iov_len);
			buf += req->iov[j].iov_len;
			total_len += req->iov[j].iov_len;
		}

		/* zero any remainder */
		remainder = vol->params.chunk_size - total_len;
		total_len += remainder;
		if (req->rmw == false && remainder) {
			memset(buf, 0, remainder);
		}
		assert(total_len == vol->params.chunk_size);
	}

	for (i = 0; i < req->num_io_units; i++) {
		success = queue_dequeue(&vol->free_backing_blocks_queue, &free_index);
		if (success) {
			req->chunk->io_unit_index[i] = free_index;
		} else {
			req->chunk->io_unit_index[i] = spdk_bit_array_find_first_clear(vol->allocated_backing_io_units,
						       vol->find_block_offset);
			vol->find_block_offset = req->chunk->io_unit_index[i] + 1;
		}
		/* TODO: fail if no backing block found - but really this should also not
		 * happen (see comment above).
		 */
		assert(req->chunk->io_unit_index[i] != REDUCE_EMPTY_MAP_ENTRY);
		spdk_bit_array_set(vol->allocated_backing_io_units, req->chunk->io_unit_index[i]);
		vol->info.allocated_io_units++;
	}
	SPDK_INFOLOG(reduce, "datareq %p, use %lu. %lu %lu %lu %lu\n", req,
		     req->chunk_map_index, req->chunk->io_unit_index[0], req->chunk->io_unit_index[1],
		     req->chunk->io_unit_index[2], req->chunk->io_unit_index[3]);
	_issue_backing_ops(req, vol, next_fn, true /* write */);
}

static void
_write_compress_done(void *_req, int reduce_errno)
{
	struct spdk_reduce_vol_request *req = _req;

	/* Negative reduce_errno indicates failure for compression operations.
	 * Just write the uncompressed data instead.  Force this to happen
	 * by just passing the full chunk size to _reduce_vol_write_chunk.
	 * When it sees the data couldn't be compressed, it will just write
	 * the uncompressed buffer to disk.
	 */
	if (reduce_errno < 0) {
		req->backing_cb_args.output_size = req->vol->params.chunk_size;
	}

	_reduce_vol_write_chunk(req, _write_write_done, req->backing_cb_args.output_size);
}

static void
_reduce_vol_compress_chunk(struct spdk_reduce_vol_request *req, reduce_request_fn next_fn)
{
	struct spdk_reduce_vol *vol = req->vol;

	req->backing_cb_args.cb_fn = next_fn;
	req->backing_cb_args.cb_arg = req;
	req->comp_buf_iov[0].iov_base = req->comp_buf;
	req->comp_buf_iov[0].iov_len = vol->params.chunk_size;
	vol->backing_dev->compress(vol->backing_dev,
				   req->decomp_iov, req->decomp_iovcnt, req->comp_buf_iov, 1,
				   &req->backing_cb_args);
}

static void
_reduce_vol_decompress_chunk_scratch(struct spdk_reduce_vol_request *req, reduce_request_fn next_fn)
{
	struct spdk_reduce_vol *vol = req->vol;

	req->backing_cb_args.cb_fn = next_fn;
	req->backing_cb_args.cb_arg = req;
	req->comp_buf_iov[0].iov_base = req->comp_buf;
	req->comp_buf_iov[0].iov_len = req->chunk->compressed_size;
	req->decomp_buf_iov[0].iov_base = req->decomp_buf;
	req->decomp_buf_iov[0].iov_len = vol->params.chunk_size;
	vol->backing_dev->decompress(vol->backing_dev,
				     req->comp_buf_iov, 1, req->decomp_buf_iov, 1,
				     &req->backing_cb_args);
}

static void
_reduce_vol_decompress_chunk(struct spdk_reduce_vol_request *req, reduce_request_fn next_fn)
{
	struct spdk_reduce_vol *vol = req->vol;
	uint64_t chunk_offset, remainder = 0;
	uint64_t ttl_len = 0;
	size_t iov_len;
	int i;

	req->decomp_iovcnt = 0;
	chunk_offset = req->offset % vol->logical_blocks_per_chunk;

	/* If backing device doesn't support SGL output then we should copy the result of decompression to user's buffer
	 * if at least one of the conditions below is true:
	 * 1. User's buffer is fragmented
	 * 2. Length of the user's buffer is less than the chunk
	 * 3. User's buffer is contig, equals chunk_size but crosses huge page boundary */
	iov_len = req->iov[0].iov_len;
	req->copy_after_decompress = !vol->backing_dev->sgl_out && (req->iovcnt > 1 ||
				     req->iov[0].iov_len < vol->params.chunk_size ||
				     _addr_crosses_huge_page(req->iov[0].iov_base, &iov_len));
	if (req->copy_after_decompress) {
		req->decomp_iov[0].iov_base = req->decomp_buf;
		req->decomp_iov[0].iov_len = vol->params.chunk_size;
		req->decomp_iovcnt = 1;
		goto decompress;
	}

	if (chunk_offset) {
		/* first iov point to our scratch buffer for any offset into the chunk */
		req->decomp_iov[0].iov_base = req->decomp_buf;
		req->decomp_iov[0].iov_len = chunk_offset * vol->params.logical_block_size;
		ttl_len += req->decomp_iov[0].iov_len;
		req->decomp_iovcnt = 1;
	}

	/* now the user data iov, direct to the user buffer */
	for (i = 0; i < req->iovcnt; i++) {
		req->decomp_iov[i + req->decomp_iovcnt].iov_base = req->iov[i].iov_base;
		req->decomp_iov[i + req->decomp_iovcnt].iov_len = req->iov[i].iov_len;
		ttl_len += req->decomp_iov[i + req->decomp_iovcnt].iov_len;
	}
	req->decomp_iovcnt += req->iovcnt;

	/* send the rest of the chunk to our scratch buffer */
	remainder = vol->params.chunk_size - ttl_len;
	if (remainder) {
		req->decomp_iov[req->decomp_iovcnt].iov_base = req->decomp_buf + ttl_len;
		req->decomp_iov[req->decomp_iovcnt].iov_len = remainder;
		ttl_len += req->decomp_iov[req->decomp_iovcnt].iov_len;
		req->decomp_iovcnt++;
	}
	assert(ttl_len == vol->params.chunk_size);

decompress:
	assert(!req->copy_after_decompress || (req->copy_after_decompress && req->decomp_iovcnt == 1));
	req->backing_cb_args.cb_fn = next_fn;
	req->backing_cb_args.cb_arg = req;
	req->comp_buf_iov[0].iov_base = req->comp_buf;
	req->comp_buf_iov[0].iov_len = req->chunk->compressed_size;
	vol->backing_dev->decompress(vol->backing_dev,
				     req->comp_buf_iov, 1, req->decomp_iov, req->decomp_iovcnt,
				     &req->backing_cb_args);
}

static inline void
_prepare_compress_chunk_copy_user_buffers(struct spdk_reduce_vol_request *req, bool zero_paddings)
{
	struct spdk_reduce_vol *vol = req->vol;
	uint64_t chunk_offset, ttl_len = 0;
	uint64_t remainder = 0;
	char *copy_offset = NULL;
	uint32_t lbsize = vol->params.logical_block_size;
	int i;

	req->decomp_iov[0].iov_base = req->decomp_buf;
	req->decomp_iov[0].iov_len = vol->params.chunk_size;
	req->decomp_iovcnt = 1;
	copy_offset = req->decomp_iov[0].iov_base;
	chunk_offset = req->offset % vol->logical_blocks_per_chunk;

	if (chunk_offset) {
		ttl_len += chunk_offset * lbsize;
		/* copy_offset already points to the correct buffer if zero_paddings=false */
		if (zero_paddings) {
			memset(copy_offset, 0, ttl_len);
		}
		copy_offset += ttl_len;
	}

	/* now the user data iov, direct from the user buffer */
	for (i = 0; i < req->iovcnt; i++) {
		memcpy(copy_offset, req->iov[i].iov_base, req->iov[i].iov_len);
		copy_offset += req->iov[i].iov_len;
		ttl_len += req->iov[i].iov_len;
	}

	remainder = vol->params.chunk_size - ttl_len;
	if (remainder) {
		/* copy_offset already points to the correct buffer if zero_paddings=false */
		if (zero_paddings) {
			memset(copy_offset, 0, remainder);
		}
		ttl_len += remainder;
	}

	assert(ttl_len == req->vol->params.chunk_size);
}

/* This function can be called when we are compressing a new data or in case of read-modify-write
 * In the first case possible paddings should be filled with zeroes, in the second case the paddings
 * should point to already read and decompressed buffer */
static inline void
_prepare_compress_chunk(struct spdk_reduce_vol_request *req, bool zero_paddings)
{
	struct spdk_reduce_vol *vol = req->vol;
	char *padding_buffer = zero_paddings ? g_zero_buf : req->decomp_buf;
	uint64_t chunk_offset, ttl_len = 0;
	uint64_t remainder = 0;
	uint32_t lbsize = vol->params.logical_block_size;
	size_t iov_len;
	int i;

	/* If backing device doesn't support SGL input then we should copy user's buffer into decomp_buf
	 * if at least one of the conditions below is true:
	 * 1. User's buffer is fragmented
	 * 2. Length of the user's buffer is less than the chunk
	 * 3. User's buffer is contig, equals chunk_size but crosses huge page boundary */
	iov_len = req->iov[0].iov_len;
	if (!vol->backing_dev->sgl_in && (req->iovcnt > 1 ||
					  req->iov[0].iov_len < vol->params.chunk_size ||
					  _addr_crosses_huge_page(req->iov[0].iov_base, &iov_len))) {
		_prepare_compress_chunk_copy_user_buffers(req, zero_paddings);
		return;
	}

	req->decomp_iovcnt = 0;
	chunk_offset = req->offset % vol->logical_blocks_per_chunk;

	if (chunk_offset != 0) {
		ttl_len += chunk_offset * lbsize;
		req->decomp_iov[0].iov_base = padding_buffer;
		req->decomp_iov[0].iov_len = ttl_len;
		req->decomp_iovcnt = 1;
	}

	/* now the user data iov, direct from the user buffer */
	for (i = 0; i < req->iovcnt; i++) {
		req->decomp_iov[i + req->decomp_iovcnt].iov_base = req->iov[i].iov_base;
		req->decomp_iov[i + req->decomp_iovcnt].iov_len = req->iov[i].iov_len;
		ttl_len += req->iov[i].iov_len;
	}
	req->decomp_iovcnt += req->iovcnt;

	remainder = vol->params.chunk_size - ttl_len;
	if (remainder) {
		req->decomp_iov[req->decomp_iovcnt].iov_base = padding_buffer + ttl_len;
		req->decomp_iov[req->decomp_iovcnt].iov_len = remainder;
		req->decomp_iovcnt++;
		ttl_len += remainder;
	}
	assert(ttl_len == req->vol->params.chunk_size);
}

static void
_write_decompress_done(void *_req, int reduce_errno)
{
	struct spdk_reduce_vol_request *req = _req;

	/* Negative reduce_errno indicates failure for compression operations. */
	if (reduce_errno < 0) {
		_reduce_vol_complete_req(req, reduce_errno);
		return;
	}

	/* Positive reduce_errno indicates that the output size field in the backing_cb_args
	 * represents the output_size.
	 */
	if (req->backing_cb_args.output_size != req->vol->params.chunk_size) {
		_reduce_vol_complete_req(req, -EIO);
		return;
	}

	_prepare_compress_chunk(req, false);
	_reduce_vol_compress_chunk(req, _write_compress_done);
}

static void
_write_read_done(void *_req, int reduce_errno)
{
	struct spdk_reduce_vol_request *req = _req;

	if (reduce_errno != 0) {
		req->reduce_errno = reduce_errno;
	}

	assert(req->num_backing_ops > 0);
	if (--req->num_backing_ops > 0) {
		return;
	}

	if (req->reduce_errno != 0) {
		_reduce_vol_complete_req(req, req->reduce_errno);
		return;
	}

	if (req->chunk_is_compressed) {
		_reduce_vol_decompress_chunk_scratch(req, _write_decompress_done);
	} else {
		req->backing_cb_args.output_size = req->chunk->compressed_size;

		_write_decompress_done(req, 0);
	}
}

static void
_read_decompress_done(void *_req, int reduce_errno)
{
	struct spdk_reduce_vol_request *req = _req;
	struct spdk_reduce_vol *vol = req->vol;

	/* Negative reduce_errno indicates failure for compression operations. */
	if (reduce_errno < 0) {
		_reduce_vol_complete_req(req, reduce_errno);
		return;
	}

	/* Positive reduce_errno indicates that the output size field in the backing_cb_args
	 * represents the output_size.
	 */
	if (req->backing_cb_args.output_size != vol->params.chunk_size) {
		_reduce_vol_complete_req(req, -EIO);
		return;
	}

	if (req->copy_after_decompress) {
		uint64_t chunk_offset = req->offset % vol->logical_blocks_per_chunk;
		char *decomp_buffer = (char *)req->decomp_buf + chunk_offset * vol->params.logical_block_size;
		int i;

		for (i = 0; i < req->iovcnt; i++) {
			memcpy(req->iov[i].iov_base, decomp_buffer, req->iov[i].iov_len);
			decomp_buffer += req->iov[i].iov_len;
			assert(decomp_buffer <= (char *)req->decomp_buf + vol->params.chunk_size);
		}
	}

	_reduce_vol_complete_req(req, 0);
}

static void
_read_read_done(void *_req, int reduce_errno)
{
	struct spdk_reduce_vol_request *req = _req;
	uint64_t chunk_offset;
	uint8_t *buf;
	int i;

	if (reduce_errno != 0) {
		req->reduce_errno = reduce_errno;
	}

	assert(req->num_backing_ops > 0);
	if (--req->num_backing_ops > 0) {
		return;
	}

	if (req->reduce_errno != 0) {
		_reduce_vol_complete_req(req, req->reduce_errno);
		return;
	}

	if (req->chunk_is_compressed) {
		_reduce_vol_decompress_chunk(req, _read_decompress_done);
	} else {

		/* If the chunk was compressed, the data would have been sent to the
		 *  host buffers by the decompression operation, if not we need to memcpy here.
		 */
		chunk_offset = req->offset % req->vol->logical_blocks_per_chunk;
		buf = req->decomp_buf + chunk_offset * req->vol->params.logical_block_size;
		for (i = 0; i < req->iovcnt; i++) {
			memcpy(req->iov[i].iov_base, buf, req->iov[i].iov_len);
			buf += req->iov[i].iov_len;
		}

		req->backing_cb_args.output_size = req->chunk->compressed_size;

		_read_decompress_done(req, 0);
	}
}

static void
_reduce_meta_read_chunk_map_for_read_chunk(void *_meta_req, int error)
{
	struct reduce_meta_request *meta_req = _meta_req;
	struct spdk_reduce_vol *vol = meta_req->restore_ctx.read_chunk_ctx.vol;
	struct spdk_reduce_vol_request *req = meta_req->restore_ctx.read_chunk_ctx.req;
	reduce_request_fn next_fn = meta_req->restore_ctx.read_chunk_ctx.next_fn;

	if (error != 0) {
		next_fn(req, error);
		_reduce_metablock_cache_read_done_update(meta_req, error);
		return;
	}

	/* copy the elem memory from metablock */
	req->chunk = req->prealloc_chunk;
	memcpy(req->chunk, _reduce_get_meta_elem_addr(meta_req),
	       _reduce_vol_get_chunk_struct_size(vol->backing_io_units_per_chunk));
	_reduce_metablock_cache_read_done_update(meta_req, error);

	assert(req->chunk->compressed_size != UINT32_MAX);
	req->num_io_units = spdk_divide_round_up(req->chunk->compressed_size,
			    vol->params.backing_io_unit_size);
	req->chunk_is_compressed = (req->num_io_units != vol->backing_io_units_per_chunk);

	_issue_backing_ops(req, vol, next_fn, false /* read */);
}

static void
_reduce_meta_read_logical_map_for_read_chunk(void *_meta_req, int error)
{
	struct reduce_meta_request *meta_req = _meta_req, *chunkmap_meta_req;
	struct spdk_reduce_vol *vol = meta_req->restore_ctx.read_chunk_ctx.vol;
	struct spdk_reduce_vol_request *req = meta_req->restore_ctx.read_chunk_ctx.req;
	reduce_request_fn next_fn = meta_req->restore_ctx.read_chunk_ctx.next_fn;

	if (error != 0) {
		next_fn(req, error);
		_reduce_metablock_cache_read_done_update(meta_req, error);
		return;
	}

	/* read the meta elem */
	req->chunk_map_index = *(uint64_t *)_reduce_get_meta_elem_addr(meta_req);
	_reduce_metablock_cache_read_done_update(meta_req, error);

	/* check chunk allocated */
	if (req->chunk_map_index == REDUCE_EMPTY_MAP_ENTRY) {
		for (int i = 0; i < req->iovcnt; i++) {
			memset(req->iov[i].iov_base, 0, req->iov[i].iov_len);
		}
		_reduce_vol_complete_req(req, 0);
		return;
	}

	/* read chunkmap */
	chunkmap_meta_req = _get_init_meta_req(vol, false, REDUCE_MTYPE_CHUNK_MAP,
					       _meta_get_mblksn(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
					       _meta_get_elem_sn_on_mblk(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
					       _reduce_meta_read_chunk_map_for_read_chunk);
	chunkmap_meta_req->restore_ctx.read_chunk_ctx.vol = vol;
	chunkmap_meta_req->restore_ctx.read_chunk_ctx.req = req;
	chunkmap_meta_req->restore_ctx.read_chunk_ctx.next_fn = next_fn;
	_reduce_metablock_cache_read(chunkmap_meta_req);

	return;
}

static void
_reduce_vol_read_chunk(struct spdk_reduce_vol_request *req, reduce_request_fn next_fn)
{
	struct spdk_reduce_vol *vol = req->vol;
	struct reduce_meta_request *meta_req = NULL;

	if (vol->params.meta_builtin) {
		if (next_fn == _write_read_done) {
			meta_req = _get_init_meta_req(vol, false, REDUCE_MTYPE_CHUNK_MAP,
						      _meta_get_mblksn(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
						      _meta_get_elem_sn_on_mblk(&vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
						      _reduce_meta_read_chunk_map_for_read_chunk);
		} else if (next_fn == _read_read_done) {
			meta_req = _get_init_meta_req(vol, false, REDUCE_MTYPE_LOGICAL_MAP,
						      _meta_get_mblksn(&vol->params, REDUCE_MTYPE_LOGICAL_MAP, req->logical_map_index),
						      _meta_get_elem_sn_on_mblk(&vol->params, REDUCE_MTYPE_LOGICAL_MAP, req->logical_map_index),
						      _reduce_meta_read_logical_map_for_read_chunk);
		} else {
			assert(0);
		}
		meta_req->restore_ctx.read_chunk_ctx.vol = vol;
		meta_req->restore_ctx.read_chunk_ctx.req = req;
		meta_req->restore_ctx.read_chunk_ctx.next_fn = next_fn;
		_reduce_metablock_cache_read(meta_req);

		return;
	}

	req->chunk_map_index = vol->pm_logical_map[req->logical_map_index];
	assert(req->chunk_map_index != REDUCE_EMPTY_MAP_ENTRY);

	req->chunk = _reduce_vol_get_chunk_map(vol, req->chunk_map_index);
	req->num_io_units = spdk_divide_round_up(req->chunk->compressed_size,
			    vol->params.backing_io_unit_size);
	req->chunk_is_compressed = (req->num_io_units != vol->backing_io_units_per_chunk);

	_issue_backing_ops(req, vol, next_fn, false /* read */);
}

static bool
_iov_array_is_valid(struct spdk_reduce_vol *vol, struct iovec *iov, int iovcnt,
		    uint64_t length)
{
	uint64_t size = 0;
	int i;

	if (iovcnt > REDUCE_MAX_IOVECS) {
		return false;
	}

	for (i = 0; i < iovcnt; i++) {
		size += iov[i].iov_len;
	}

	return size == (length * vol->params.logical_block_size);
}

static bool
_check_overlap(struct spdk_reduce_vol *vol, uint64_t logical_map_index)
{
	struct spdk_reduce_vol_request req;

	req.logical_map_index = logical_map_index;

	return (NULL != RB_FIND(executing_req_tree, &vol->executing_requests, &req));
}

static void
_start_readv_request(struct spdk_reduce_vol_request *req)
{
	RB_INSERT(executing_req_tree, &req->vol->executing_requests, req);
	_reduce_vol_read_chunk(req, _read_read_done);
}

void
spdk_reduce_vol_readv(struct spdk_reduce_vol *vol,
		      struct iovec *iov, int iovcnt, uint64_t offset, uint64_t length,
		      spdk_reduce_vol_op_complete cb_fn, void *cb_arg)
{
	struct spdk_reduce_vol_request *req;
	uint64_t logical_map_index;
	bool overlapped;
	int i;

	if (length == 0) {
		cb_fn(cb_arg, 0);
		return;
	}

	if (_request_spans_chunk_boundary(vol, offset, length)) {
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	if (!_iov_array_is_valid(vol, iov, iovcnt, length)) {
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	logical_map_index = offset / vol->logical_blocks_per_chunk;
	overlapped = _check_overlap(vol, logical_map_index);

	/* If we use builtin metadata, chunk allocated detection needs async read backend.
	 * and get the metadata async and check the chunk allocated status, maybe late,
	 * there may be another write about the chunk.
	 * So we need insert the overlap tree, then check the chunk allocated status.
	 */
	if (!vol->params.meta_builtin) {
		if (!overlapped && vol->pm_logical_map[logical_map_index] == REDUCE_EMPTY_MAP_ENTRY) {
			/*
			 * This chunk hasn't been allocated.  So treat the data as all
			 * zeroes for this chunk - do the memset and immediately complete
			 * the operation.
			 */
			for (i = 0; i < iovcnt; i++) {
				memset(iov[i].iov_base, 0, iov[i].iov_len);
			}
			cb_fn(cb_arg, 0);
			return;
		}
	}

	req = TAILQ_FIRST(&vol->free_requests);
	if (req == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	TAILQ_REMOVE(&vol->free_requests, req, tailq);
	req->type = REDUCE_IO_READV;
	req->vol = vol;
	req->iov = iov;
	req->iovcnt = iovcnt;
	req->offset = offset;
	req->logical_map_index = logical_map_index;
	req->length = length;
	req->copy_after_decompress = false;
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
	req->reduce_errno = 0;

	if (!overlapped) {
		_start_readv_request(req);
	} else {
		TAILQ_INSERT_TAIL(&vol->queued_requests, req, tailq);
	}
}

static void
_reduce_meta_read_logical_map_for_writev(void *_meta_req, int error)
{
	struct reduce_meta_request *meta_req = _meta_req;
	struct spdk_reduce_vol_request *req = meta_req->restore_ctx.req_ctx.req;
	struct spdk_reduce_vol *vol = req->vol;

	if (error != 0) {
		req->reduce_errno = error;
		_reduce_vol_complete_req(req, req->reduce_errno);
		return;
	}

	/* read the meta elem */
	req->chunk_map_index = *(uint64_t *)_reduce_get_meta_elem_addr(meta_req);
	req->read_chunk_map_index = req->chunk_map_index;
	_reduce_metablock_cache_read_done_update(meta_req, error);

	/* restore */
	if (req->chunk_map_index != REDUCE_EMPTY_MAP_ENTRY) {
		if ((req->length * vol->params.logical_block_size) < vol->params.chunk_size) {
			/* Read old chunk, then overwrite with data from this write
			 *  operation.
			 */
			req->rmw = true;
			_reduce_vol_read_chunk(req, _write_read_done);
			return;
		}
	}

	req->rmw = false;

	_prepare_compress_chunk(req, true);
	_reduce_vol_compress_chunk(req, _write_compress_done);
}

static void
_start_writev_request(struct spdk_reduce_vol_request *req)
{
	struct spdk_reduce_vol *vol = req->vol;
	struct reduce_meta_request *meta_req;

	RB_INSERT(executing_req_tree, &req->vol->executing_requests, req);

	if (vol->params.meta_builtin) {
		meta_req = _get_init_meta_req(vol, false, REDUCE_MTYPE_LOGICAL_MAP,
					      _meta_get_mblksn(&vol->params, REDUCE_MTYPE_LOGICAL_MAP, req->logical_map_index),
					      _meta_get_elem_sn_on_mblk(&vol->params, REDUCE_MTYPE_LOGICAL_MAP, req->logical_map_index),
					      _reduce_meta_read_logical_map_for_writev);
		meta_req->restore_ctx.req_ctx.req = req;

		_reduce_metablock_cache_read(meta_req);
		return;
	}

	if (vol->pm_logical_map[req->logical_map_index] != REDUCE_EMPTY_MAP_ENTRY) {
		if ((req->length * vol->params.logical_block_size) < vol->params.chunk_size) {
			/* Read old chunk, then overwrite with data from this write
			 *  operation.
			 */
			req->rmw = true;
			_reduce_vol_read_chunk(req, _write_read_done);
			return;
		}
	}

	req->rmw = false;

	_prepare_compress_chunk(req, true);
	_reduce_vol_compress_chunk(req, _write_compress_done);
}

void
spdk_reduce_vol_writev(struct spdk_reduce_vol *vol,
		       struct iovec *iov, int iovcnt, uint64_t offset, uint64_t length,
		       spdk_reduce_vol_op_complete cb_fn, void *cb_arg)
{
	struct spdk_reduce_vol_request *req;
	uint64_t logical_map_index;
	bool overlapped;

	if (length == 0) {
		cb_fn(cb_arg, 0);
		return;
	}

	if (_request_spans_chunk_boundary(vol, offset, length)) {
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	if (!_iov_array_is_valid(vol, iov, iovcnt, length)) {
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	logical_map_index = offset / vol->logical_blocks_per_chunk;
	overlapped = _check_overlap(vol, logical_map_index);

	req = TAILQ_FIRST(&vol->free_requests);
	if (req == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	TAILQ_REMOVE(&vol->free_requests, req, tailq);
	req->type = REDUCE_IO_WRITEV;
	req->vol = vol;
	req->iov = iov;
	req->iovcnt = iovcnt;
	req->offset = offset;
	req->logical_map_index = logical_map_index;
	req->length = length;
	req->copy_after_decompress = false;
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
	req->reduce_errno = 0;

	if (!overlapped) {
		_start_writev_request(req);
	} else {
		TAILQ_INSERT_TAIL(&vol->queued_requests, req, tailq);
	}
}

static void
_unmap_chunkmap_update_error_process(struct reduce_meta_request *chunk_map_meta_req, int error)
{
	struct reduce_meta_request *logical_map_meta_req =
			chunk_map_meta_req->restore_ctx.req_ctx.logicalmap_meta_req;
	struct spdk_reduce_vol_request *req = chunk_map_meta_req->restore_ctx.req_ctx.req;

	if (chunk_map_meta_req->supply_read || !chunk_map_meta_req->is_write) {
		_reduce_metablock_cache_read_done_update(chunk_map_meta_req, error);
	} else {
		_reduce_metablock_cache_write_done_update(chunk_map_meta_req, error);
	}
	_reduce_metablock_cache_read_done_update(logical_map_meta_req, error);
	_reduce_vol_complete_req(req, error);
}

static void
_reduce_meta_read_chunk_map_for_unmap_full_chunk(void *cb_arg, int reduce_errno)
{
	struct reduce_meta_request *chunk_map_meta_req = cb_arg;
	struct spdk_reduce_vol_request *req = chunk_map_meta_req->restore_ctx.req_ctx.req;
	struct spdk_reduce_vol *vol = req->vol;
	struct reduce_meta_request *logical_map_meta_req =
			chunk_map_meta_req->restore_ctx.req_ctx.logicalmap_meta_req;
	struct spdk_reduce_chunk_map *chunk_map;
	uint64_t *logical_map;

	if (chunk_map_meta_req->supply_read) {
		if (reduce_errno != 0) {
			_unmap_chunkmap_update_error_process(chunk_map_meta_req, reduce_errno);
			return;
		}

		chunk_map = (struct spdk_reduce_chunk_map *)_reduce_get_meta_elem_addr(chunk_map_meta_req);
		_reduce_vol_reset_chunk(vol, req->chunk_map_index, chunk_map);
		convert_supply_read_to_write(chunk_map_meta_req);
		_reduce_metablock_cache_read_done_update(chunk_map_meta_req, reduce_errno);
	} else {
		if (reduce_errno != 0) {
			_unmap_chunkmap_update_error_process(chunk_map_meta_req, reduce_errno);
			return;
		}
		_reduce_metablock_cache_write_done_update(chunk_map_meta_req, reduce_errno);

		/* clear logical map */
		logical_map = (uint64_t *)_reduce_get_meta_elem_addr(logical_map_meta_req);
		*logical_map = REDUCE_EMPTY_MAP_ENTRY;
		convert_supply_read_to_write(logical_map_meta_req);
		_reduce_metablock_cache_read_done_update(logical_map_meta_req, reduce_errno);
	}
}

static void
_reduce_meta_read_logical_map_for_unmap_full_chunk(void *cb_arg, int reduce_errno)
{
	struct reduce_meta_request *logical_map_meta_req = cb_arg, *chunk_map_meta_req;
	struct spdk_reduce_vol_request *req = logical_map_meta_req->restore_ctx.req_ctx.req;

	if (logical_map_meta_req->supply_read) {
		/* the entry process of unmap process */
		if (reduce_errno != 0) {
			_reduce_metablock_cache_read_done_update(logical_map_meta_req, reduce_errno);
			_reduce_vol_complete_req(req, reduce_errno);
			return;
		}

		req->chunk_map_index = *(uint64_t *)_reduce_get_meta_elem_addr(logical_map_meta_req);
		if (req->chunk_map_index == REDUCE_EMPTY_MAP_ENTRY) {
			_reduce_metablock_cache_read_done_update(logical_map_meta_req, reduce_errno);
			_reduce_vol_complete_req(req, reduce_errno);
			return;
		}

		/* get_chunk_map */
		chunk_map_meta_req = _get_init_meta_req(req->vol, true, REDUCE_MTYPE_CHUNK_MAP,
							_meta_get_mblksn(&req->vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
							_meta_get_elem_sn_on_mblk(&req->vol->params, REDUCE_MTYPE_CHUNK_MAP, req->chunk_map_index),
							_reduce_meta_read_chunk_map_for_unmap_full_chunk);
		chunk_map_meta_req->restore_ctx.req_ctx.req = req;
		chunk_map_meta_req->restore_ctx.req_ctx.logicalmap_meta_req = logical_map_meta_req;
		_reduce_metablock_cache_read(chunk_map_meta_req);
	} else {
		/* the final process of unmap process */
		_reduce_metablock_cache_write_done_update(logical_map_meta_req, reduce_errno);
		_reduce_vol_complete_req(req, reduce_errno);
	}
}

static void
_reduce_meta_unmap_full_chunk(void *ctx)
{
	struct spdk_reduce_vol_request *req = ctx;
	struct reduce_meta_request *logical_map_meta_req;

	/* alloc write meta_req, if we find the logical map is EMPTY, don't need write */
	logical_map_meta_req = _get_init_meta_req(req->vol, true, REDUCE_MTYPE_LOGICAL_MAP,
			       _meta_get_mblksn(&req->vol->params, REDUCE_MTYPE_LOGICAL_MAP, req->logical_map_index),
			       _meta_get_elem_sn_on_mblk(&req->vol->params, REDUCE_MTYPE_LOGICAL_MAP, req->logical_map_index),
			       _reduce_meta_read_logical_map_for_unmap_full_chunk);
	logical_map_meta_req->restore_ctx.req_ctx.req = req;
	_reduce_metablock_cache_read(logical_map_meta_req);
}

static void
_start_unmap_request_full_chunk(void *ctx)
{
	struct spdk_reduce_vol_request *req = ctx;
	struct spdk_reduce_vol *vol = req->vol;
	uint64_t chunk_map_index;

	RB_INSERT(executing_req_tree, &req->vol->executing_requests, req);

	if (vol->params.meta_builtin) {
		_reduce_meta_unmap_full_chunk(ctx);
		return;
	}

	chunk_map_index = vol->pm_logical_map[req->logical_map_index];
	if (chunk_map_index != REDUCE_EMPTY_MAP_ENTRY) {
		_reduce_vol_reset_chunk(vol, chunk_map_index, NULL);
		req->chunk = _reduce_vol_get_chunk_map(vol, req->chunk_map_index);
		_reduce_persist(vol, req->chunk,
				_reduce_vol_get_chunk_struct_size(vol->backing_io_units_per_chunk));
		vol->pm_logical_map[req->logical_map_index] = REDUCE_EMPTY_MAP_ENTRY;
		_reduce_persist(vol, &vol->pm_logical_map[req->logical_map_index], sizeof(uint64_t));
	}
	_reduce_vol_complete_req(req, 0);
}

static void
_reduce_vol_unmap_full_chunk(struct spdk_reduce_vol *vol,
			     uint64_t offset, uint64_t length,
			     spdk_reduce_vol_op_complete cb_fn, void *cb_arg)
{
	struct spdk_reduce_vol_request *req;
	uint64_t logical_map_index;
	bool overlapped;

	if (_request_spans_chunk_boundary(vol, offset, length)) {
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	logical_map_index = offset / vol->logical_blocks_per_chunk;
	overlapped = _check_overlap(vol, logical_map_index);

	req = TAILQ_FIRST(&vol->free_requests);
	if (req == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	TAILQ_REMOVE(&vol->free_requests, req, tailq);
	req->type = REDUCE_IO_UNMAP;
	req->vol = vol;
	req->iov = NULL;
	req->iovcnt = 0;
	req->offset = offset;
	req->logical_map_index = logical_map_index;
	req->length = length;
	req->copy_after_decompress = false;
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
	req->reduce_errno = 0;

	if (!overlapped) {
		_start_unmap_request_full_chunk(req);
	} else {
		TAILQ_INSERT_TAIL(&vol->queued_requests, req, tailq);
	}
}

struct unmap_partial_chunk_ctx {
	struct spdk_reduce_vol *vol;
	struct iovec iov;
	spdk_reduce_vol_op_complete cb_fn;
	void *cb_arg;
};

static void
_reduce_unmap_partial_chunk_complete(void *_ctx, int reduce_errno)
{
	struct unmap_partial_chunk_ctx *ctx = _ctx;

	ctx->cb_fn(ctx->cb_arg, reduce_errno);
	free(ctx);
}

static void
_reduce_vol_unmap_partial_chunk(struct spdk_reduce_vol *vol, uint64_t offset, uint64_t length,
				spdk_reduce_vol_op_complete cb_fn, void *cb_arg)
{
	struct unmap_partial_chunk_ctx *ctx;

	ctx = calloc(1, sizeof(struct unmap_partial_chunk_ctx));
	if (ctx == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	ctx->vol = vol;
	ctx->iov.iov_base = g_zero_buf;
	ctx->iov.iov_len = length * vol->params.logical_block_size;
	ctx->cb_fn = cb_fn;
	ctx->cb_arg = cb_arg;

	spdk_reduce_vol_writev(vol, &ctx->iov, 1, offset, length, _reduce_unmap_partial_chunk_complete,
			       ctx);
}

void
spdk_reduce_vol_unmap(struct spdk_reduce_vol *vol,
		      uint64_t offset, uint64_t length,
		      spdk_reduce_vol_op_complete cb_fn, void *cb_arg)
{
	if (length < vol->logical_blocks_per_chunk) {
		_reduce_vol_unmap_partial_chunk(vol, offset, length, cb_fn, cb_arg);
	} else if (length == vol->logical_blocks_per_chunk) {
		_reduce_vol_unmap_full_chunk(vol, offset, length, cb_fn, cb_arg);
	} else {
		cb_fn(cb_arg, -EINVAL);
	}
}

const struct spdk_reduce_vol_params *
spdk_reduce_vol_get_params(struct spdk_reduce_vol *vol)
{
	return &vol->params;
}

const char *
spdk_reduce_vol_get_pm_path(const struct spdk_reduce_vol *vol)
{
	return vol->pm_file.path;
}

void
spdk_reduce_vol_print_info(struct spdk_reduce_vol *vol)
{
	uint64_t logical_map_size, num_chunks, ttl_chunk_sz;
	uint32_t struct_size;
	uint64_t chunk_map_size;

	SPDK_NOTICELOG("vol info:\n");
	SPDK_NOTICELOG("\tvol->params.backing_io_unit_size = 0x%x\n", vol->params.backing_io_unit_size);
	SPDK_NOTICELOG("\tvol->params.logical_block_size = 0x%x\n", vol->params.logical_block_size);
	SPDK_NOTICELOG("\tvol->params.chunk_size = 0x%x\n", vol->params.chunk_size);
	SPDK_NOTICELOG("\tvol->params.vol_size = 0x%" PRIx64 "\n", vol->params.vol_size);
	num_chunks = _get_total_chunks(vol->params.vol_size, vol->params.chunk_size);
	SPDK_NOTICELOG("\ttotal chunks (including extra) = 0x%" PRIx64 "\n", num_chunks);
	SPDK_NOTICELOG("\ttotal chunks (excluding extra) = 0x%" PRIx64 "\n",
		       vol->params.vol_size / vol->params.chunk_size);
	ttl_chunk_sz = _get_pm_total_chunks_size(vol->params.vol_size, vol->params.chunk_size,
			vol->params.backing_io_unit_size);
	SPDK_NOTICELOG("\ttotal_chunks_size = 0x%" PRIx64 "\n", ttl_chunk_sz);
	struct_size = _reduce_vol_get_chunk_struct_size(vol->backing_io_units_per_chunk);
	SPDK_NOTICELOG("\tchunk_struct_size = 0x%x\n", struct_size);

	SPDK_NOTICELOG("pmem info:\n");
	SPDK_NOTICELOG("\tvol->pm_file.size = 0x%" PRIx64 "\n", vol->pm_file.size);
	SPDK_NOTICELOG("\tvol->pm_file.pm_buf = %p\n", (void *)vol->pm_file.pm_buf);
	SPDK_NOTICELOG("\tvol->pm_super = %p\n", (void *)vol->pm_super);
	SPDK_NOTICELOG("\tvol->pm_logical_map = %p\n", (void *)vol->pm_logical_map);
	logical_map_size = _get_pm_logical_map_size(vol->params.vol_size,
			   vol->params.chunk_size);
	SPDK_NOTICELOG("\tlogical_map_size = 0x%" PRIx64 "\n", logical_map_size);
	SPDK_NOTICELOG("\tvol->pm_chunk_maps = %p\n", (void *)vol->pm_chunk_maps);
	chunk_map_size = _get_pm_total_chunks_size(vol->params.vol_size, vol->params.chunk_size,
			 vol->params.backing_io_unit_size);
	SPDK_NOTICELOG("\tchunk_map_size = 0x%" PRIx64 "\n", chunk_map_size);
}

SPDK_LOG_REGISTER_COMPONENT(reduce)
