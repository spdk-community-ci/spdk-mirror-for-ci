/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2019 Intel Corporation.
 *   All rights reserved.
 */

#include <ocf/ocf.h>

#include "spdk/bdev_module.h"
#include "spdk/env.h"
#include "spdk/thread.h"
#include "spdk/log.h"

#include "data.h"
#include "volume.h"
#include "ctx.h"
#include "vbdev_ocf.h"

static int
vbdev_ocf_volume_open(ocf_volume_t volume, void *opts)
{
	struct vbdev_ocf_base **priv = ocf_volume_get_priv(volume);
	struct vbdev_ocf_base *base;

	if (opts) {
		base = opts;
	} else {
		base = vbdev_ocf_get_base_by_name(ocf_volume_get_uuid(volume)->data);
		if (base == NULL) {
			return -ENODEV;
		}
	}

	*priv = base;

	return 0;
}

static void
vbdev_ocf_volume_close(ocf_volume_t volume)
{
}

static uint64_t
vbdev_ocf_volume_get_length(ocf_volume_t volume)
{
	struct vbdev_ocf_base *base = *((struct vbdev_ocf_base **)ocf_volume_get_priv(volume));
	uint64_t len;

	len = base->bdev->blocklen * base->bdev->blockcnt;

	return len;
}

static int
vbdev_ocf_volume_io_set_data(struct ocf_io *io, ctx_data_t *data,
			     uint32_t offset)
{
	struct ocf_io_ctx *io_ctx = ocf_get_io_ctx(io);

	io_ctx->offset = offset;
	io_ctx->data = data;

	assert(io_ctx->data != NULL);
	if (io_ctx->data->iovs && offset >= io_ctx->data->size) {
		return -ENOBUFS;
	}

	return 0;
}

static ctx_data_t *
vbdev_ocf_volume_io_get_data(struct ocf_io *io)
{
	return ocf_get_io_ctx(io)->data;
}

static void
vbdev_ocf_volume_io_get(struct ocf_io *io)
{
	struct ocf_io_ctx *io_ctx = ocf_get_io_ctx(io);

	io_ctx->ref++;
}

static void
vbdev_ocf_volume_io_put(struct ocf_io *io)
{
	struct ocf_io_ctx *io_ctx = ocf_get_io_ctx(io);

	if (--io_ctx->ref) {
		return;
	}
}

static int
get_starting_vec(struct iovec *iovs, int iovcnt, uint64_t *offset)
{
	int i;
	size_t off;

	off = *offset;

	for (i = 0; i < iovcnt; i++) {
		if (off < iovs[i].iov_len) {
			*offset = off;
			return i;
		}
		off -= iovs[i].iov_len;
	}

	return -1;
}

static void
initialize_cpy_vector(struct iovec *cpy_vec, int cpy_vec_len, struct iovec *orig_vec,
		      int orig_vec_len,
		      size_t offset, size_t bytes)
{
	void *curr_base;
	int len, i;

	i = 0;

	while (bytes > 0) {
		curr_base = orig_vec[i].iov_base + offset;
		len = MIN(bytes, orig_vec[i].iov_len - offset);

		cpy_vec[i].iov_base = curr_base;
		cpy_vec[i].iov_len = len;

		bytes -= len;
		offset = 0;
		i++;
	}
}

static void
vbdev_ocf_volume_submit_io_cb(struct spdk_bdev_io *bdev_io, bool success, void *opaque)
{
	struct ocf_io *io;
	struct ocf_io_ctx *io_ctx;

	assert(opaque);

	io = opaque;
	io_ctx = ocf_get_io_ctx(io);
	assert(io_ctx != NULL);

	if (!success) {
		io_ctx->error = io_ctx->error ? : -OCF_ERR_IO;
	}

	if (io_ctx->iovs_allocated && bdev_io != NULL) {
		env_free(bdev_io->u.bdev.iovs);
	}

	if (io_ctx->error) {
		SPDK_DEBUGLOG(vbdev_ocf_volume,
			      "base returned error on io submission: %d\n", io_ctx->error);
	}

	if (io->io_queue == NULL && io_ctx->ch != NULL) {
		spdk_put_io_channel(io_ctx->ch);
	}

	vbdev_ocf_volume_io_put(io);
	if (bdev_io) {
		spdk_bdev_free_io(bdev_io);
	}

	if (--io_ctx->rq_cnt == 0) {
		io->end(io, io_ctx->error);
	}
}

static int
prepare_submit(struct ocf_io *io)
{
	struct ocf_io_ctx *io_ctx = ocf_get_io_ctx(io);
	struct vbdev_ocf_qctx *qctx;
	struct vbdev_ocf_base *base;
	ocf_queue_t q = io->io_queue;
	ocf_cache_t cache;
	struct vbdev_ocf_cache_ctx *cctx;
	int rc = 0;

	io_ctx->rq_cnt++;
	if (io_ctx->rq_cnt != 1) {
		return 0;
	}

	vbdev_ocf_volume_io_get(io);
	base = *((struct vbdev_ocf_base **)ocf_volume_get_priv(ocf_io_get_volume(io)));

	if (io->io_queue == NULL) {
		/* In case IO is initiated by OCF, queue is unknown
		 * so we have to get io channel ourselves */
		io_ctx->ch = spdk_bdev_get_io_channel(base->desc);
		if (io_ctx->ch == NULL) {
			return -EPERM;
		}
		return 0;
	}

	cache = ocf_queue_get_cache(q);
	cctx = ocf_cache_get_priv(cache);
	if (cctx == NULL) {
		return -EFAULT;
	}

	if (q == cctx->mngt_queue) {
		io_ctx->ch = base->management_channel;
		return 0;
	}

	qctx = ocf_queue_get_priv(q);
	if (qctx == NULL) {
		return -EFAULT;
	}

	if (base->is_cache) {
		io_ctx->ch = qctx->cache_ch;
	} else {
		io_ctx->ch = qctx->core_ch;
	}

	return rc;
}

static void
vbdev_ocf_volume_submit_flush(struct ocf_io *io)
{
	struct vbdev_ocf_base *base =
		*((struct vbdev_ocf_base **)
		  ocf_volume_get_priv(ocf_io_get_volume(io)));
	struct ocf_io_ctx *io_ctx = ocf_get_io_ctx(io);
	int status;

	status = prepare_submit(io);
	if (status) {
		SPDK_ERRLOG("Preparing io failed with status=%d\n", status);
		vbdev_ocf_volume_submit_io_cb(NULL, false, io);
		return;
	}

	status = spdk_bdev_flush(
			 base->desc, io_ctx->ch,
			 io->addr, io->bytes,
			 vbdev_ocf_volume_submit_io_cb, io);
	if (status) {
		/* Since callback is not called, we need to do it manually to free io structures */
		SPDK_ERRLOG("Submission failed with status=%d\n", status);
		vbdev_ocf_volume_submit_io_cb(NULL, false, io);
	}
}

static void
vbdev_ocf_volume_submit_io(struct ocf_io *io)
{
	struct vbdev_ocf_base *base =
		*((struct vbdev_ocf_base **)
		  ocf_volume_get_priv(ocf_io_get_volume(io)));
	struct ocf_io_ctx *io_ctx = ocf_get_io_ctx(io);
	struct iovec *iovs;
	int iovcnt, status = 0, i;
	uint64_t addr, len, offset;

	if (io->flags == OCF_WRITE_FLUSH) {
		vbdev_ocf_volume_submit_flush(io);
		return;
	}

	status = prepare_submit(io);
	if (status) {
		SPDK_ERRLOG("Preparing io failed with status=%d\n", status);
		vbdev_ocf_volume_submit_io_cb(NULL, false, io);
		return;
	}

	/* IO fields */
	addr = io->addr;
	len = io->bytes;
	offset = io_ctx->offset;

	if (len < io_ctx->data->size) {
		if (io_ctx->data->iovcnt == 1) {
			if (io->dir == OCF_READ) {
				status = spdk_bdev_read(base->desc, io_ctx->ch,
							io_ctx->data->iovs[0].iov_base + offset, addr, len,
							vbdev_ocf_volume_submit_io_cb, io);
			} else if (io->dir == OCF_WRITE) {
				status = spdk_bdev_write(base->desc, io_ctx->ch,
							 io_ctx->data->iovs[0].iov_base + offset, addr, len,
							 vbdev_ocf_volume_submit_io_cb, io);
			}
			goto end;
		} else {
			i = get_starting_vec(io_ctx->data->iovs, io_ctx->data->iovcnt, &offset);

			if (i < 0) {
				SPDK_ERRLOG("offset bigger than data size\n");
				vbdev_ocf_volume_submit_io_cb(NULL, false, io);
				return;
			}

			iovcnt = io_ctx->data->iovcnt - i;

			io_ctx->iovs_allocated = true;
			iovs = env_malloc(sizeof(*iovs) * iovcnt, ENV_MEM_NOIO);

			if (!iovs) {
				SPDK_ERRLOG("allocation failed\n");
				vbdev_ocf_volume_submit_io_cb(NULL, false, io);
				return;
			}

			initialize_cpy_vector(iovs, io_ctx->data->iovcnt, &io_ctx->data->iovs[i],
					      iovcnt, offset, len);
		}
	} else {
		iovs = io_ctx->data->iovs;
		iovcnt = io_ctx->data->iovcnt;
	}

	if (io->dir == OCF_READ) {
		status = spdk_bdev_readv(base->desc, io_ctx->ch,
					 iovs, iovcnt, addr, len, vbdev_ocf_volume_submit_io_cb, io);
	} else if (io->dir == OCF_WRITE) {
		status = spdk_bdev_writev(base->desc, io_ctx->ch,
					  iovs, iovcnt, addr, len, vbdev_ocf_volume_submit_io_cb, io);
	}

end:
	if (status) {
		if (status == -ENOMEM) {
			io_ctx->error = -OCF_ERR_NO_MEM;
		} else {
			SPDK_ERRLOG("submission failed with status=%d\n", status);
		}

		/* Since callback is not called, we need to do it manually to free io structures */
		vbdev_ocf_volume_submit_io_cb(NULL, false, io);
	}
}

static void
vbdev_ocf_volume_submit_discard(struct ocf_io *io)
{
	struct vbdev_ocf_base *base =
		*((struct vbdev_ocf_base **)
		  ocf_volume_get_priv(ocf_io_get_volume(io)));
	struct ocf_io_ctx *io_ctx = ocf_get_io_ctx(io);
	int status = 0;

	status = prepare_submit(io);
	if (status) {
		SPDK_ERRLOG("Preparing io failed with status=%d\n", status);
		vbdev_ocf_volume_submit_io_cb(NULL, false, io);
		return;
	}

	status = spdk_bdev_unmap(
			 base->desc, io_ctx->ch,
			 io->addr, io->bytes,
			 vbdev_ocf_volume_submit_io_cb, io);
	if (status) {
		/* Since callback is not called, we need to do it manually to free io structures */
		SPDK_ERRLOG("Submission failed with status=%d\n", status);
		vbdev_ocf_volume_submit_io_cb(NULL, false, io);
	}
}

static void
vbdev_ocf_volume_submit_metadata(struct ocf_io *io)
{
	/* Implement with persistent metadata support */
}

static unsigned int
vbdev_ocf_volume_get_max_io_size(ocf_volume_t volume)
{
	return 131072;
}

static void
vbdev_forward_io_cb(struct spdk_bdev_io *bdev_io, bool success, void *opaque)
{
	ocf_forward_token_t token = (ocf_forward_token_t) opaque;

	assert(token);

	spdk_bdev_free_io(bdev_io);

	ocf_forward_end(token, success ? 0 : -OCF_ERR_IO);
}

static void
vbdev_forward_io_free_iovs_cb(struct spdk_bdev_io *bdev_io, bool success, void *opaque)
{
	env_free(bdev_io->u.bdev.iovs);
	vbdev_forward_io_cb(bdev_io, success, opaque);
}

static struct spdk_io_channel *
vbdev_forward_get_channel(ocf_volume_t volume, ocf_forward_token_t token)
{
	struct vbdev_ocf_base *base =
		*((struct vbdev_ocf_base **)
		  ocf_volume_get_priv(volume));
	ocf_queue_t queue = ocf_forward_get_io_queue(token);
	struct vbdev_ocf_qctx *qctx;

	if (unlikely(ocf_queue_is_mngt(queue))) {
		return base->management_channel;
	}

	qctx = ocf_queue_get_priv(queue);
	if (unlikely(qctx == NULL)) {
		return NULL;
	}

	return (base->is_cache) ? qctx->cache_ch : qctx->core_ch;
}

static void
vbdev_forward_io(ocf_volume_t volume, ocf_forward_token_t token,
		 int dir, uint64_t addr, uint64_t bytes,
		 uint64_t offset)
{
	struct vbdev_ocf_base *base =
		*((struct vbdev_ocf_base **)
		  ocf_volume_get_priv(volume));
	struct bdev_ocf_data *data = ocf_forward_get_data(token);
	struct spdk_io_channel *ch;
	spdk_bdev_io_completion_cb cb = vbdev_forward_io_cb;
	bool iovs_allocated = false;
	int iovcnt, skip, status = -1;
	struct iovec *iovs;

	ch = vbdev_forward_get_channel(volume, token);
	if (unlikely(ch == NULL)) {
		ocf_forward_end(token, -EFAULT);
		return;
	}

	if (bytes == data->size) {
		iovs = data->iovs;
		iovcnt = data->iovcnt;
	} else {
		skip = get_starting_vec(data->iovs, data->iovcnt, &offset);
		if (skip < 0) {
			SPDK_ERRLOG("Offset bigger than data size\n");
			ocf_forward_end(token, -OCF_ERR_IO);
			return;
		}

		iovcnt = data->iovcnt - skip;

		iovs_allocated = true;
		cb = vbdev_forward_io_free_iovs_cb;
		iovs = env_malloc(sizeof(*iovs) * iovcnt, ENV_MEM_NOIO);

		if (!iovs) {
			SPDK_ERRLOG("Allocation failed\n");
			ocf_forward_end(token, -OCF_ERR_NO_MEM);
			return;
		}

		initialize_cpy_vector(iovs, data->iovcnt, &data->iovs[skip],
				      iovcnt, offset, bytes);
	}

	if (dir == OCF_READ) {
		status = spdk_bdev_readv(base->desc, ch, iovs, iovcnt,
					 addr, bytes, cb, (void *) token);
	} else if (dir == OCF_WRITE) {
		status = spdk_bdev_writev(base->desc, ch, iovs, iovcnt,
					  addr, bytes, cb, (void *) token);
	}

	if (unlikely(status)) {
		SPDK_ERRLOG("Submission failed with status=%d\n", status);
		/* Since callback is not called, we need to do it manually to free iovs */
		if (iovs_allocated) {
			env_free(iovs);
		}
		ocf_forward_end(token, (status == -ENOMEM) ? -OCF_ERR_NO_MEM : -OCF_ERR_IO);
	}
}

static void
vbdev_forward_flush(ocf_volume_t volume, ocf_forward_token_t token)
{
	struct vbdev_ocf_base *base =
		*((struct vbdev_ocf_base **)
		  ocf_volume_get_priv(volume));
	struct spdk_io_channel *ch;
	uint64_t bytes = base->bdev->blockcnt * base->bdev->blocklen;
	int status;

	ch = vbdev_forward_get_channel(volume, token);
	if (unlikely(ch == NULL)) {
		ocf_forward_end(token, -EFAULT);
		return;
	}

	status = spdk_bdev_flush(
			 base->desc, ch, 0, bytes,
			 vbdev_forward_io_cb, (void *)token);
	if (unlikely(status)) {
		SPDK_ERRLOG("Submission failed with status=%d\n", status);
		ocf_forward_end(token, (status == -ENOMEM) ? -OCF_ERR_NO_MEM : -OCF_ERR_IO);
	}
}

static void
vbdev_forward_discard(ocf_volume_t volume, ocf_forward_token_t token,
		      uint64_t addr, uint64_t bytes)
{
	struct vbdev_ocf_base *base =
		*((struct vbdev_ocf_base **)
		  ocf_volume_get_priv(volume));
	struct spdk_io_channel *ch;
	int status = 0;

	ch = vbdev_forward_get_channel(volume, token);
	if (unlikely(ch == NULL)) {
		ocf_forward_end(token, -EFAULT);
		return;
	}

	status = spdk_bdev_unmap(
			 base->desc, ch, addr, bytes,
			 vbdev_forward_io_cb, (void *)token);
	if (unlikely(status)) {
		SPDK_ERRLOG("Submission failed with status=%d\n", status);
		ocf_forward_end(token, (status == -ENOMEM) ? -OCF_ERR_NO_MEM : -OCF_ERR_IO);
	}
}

struct vbdev_forward_io_simple_ctx {
	ocf_forward_token_t token;
	struct spdk_io_channel *ch;
};

static void
vbdev_forward_io_simple_cb(struct spdk_bdev_io *bdev_io, bool success, void *opaque)
{
	struct vbdev_forward_io_simple_ctx *ctx = opaque;
	ocf_forward_token_t token = ctx->token;

	assert(token);

	spdk_bdev_free_io(bdev_io);
	spdk_put_io_channel(ctx->ch);
	env_free(ctx);

	ocf_forward_end(token, success ? 0 : -OCF_ERR_IO);
}

static void
vbdev_forward_io_simple(ocf_volume_t volume, ocf_forward_token_t token,
			int dir, uint64_t addr, uint64_t bytes)
{
	struct vbdev_ocf_base *base =
		*((struct vbdev_ocf_base **)
		  ocf_volume_get_priv(volume));
	struct bdev_ocf_data *data = ocf_forward_get_data(token);
	struct vbdev_forward_io_simple_ctx *ctx;
	int status = -1;

	ctx = env_malloc(sizeof(*ctx), ENV_MEM_NOIO);
	if (unlikely(!ctx)) {
		ocf_forward_end(token, -OCF_ERR_NO_MEM);
		return;
	}

	/* Forward IO simple is used in context where queue is not available
	 * so we have to get io channel ourselves */
	ctx->ch = spdk_bdev_get_io_channel(base->desc);
	if (unlikely(ctx->ch == NULL)) {
		env_free(ctx);
		ocf_forward_end(token, -EFAULT);
		return;
	}

	ctx->token = token;

	if (dir == OCF_READ) {
		status = spdk_bdev_readv(base->desc, ctx->ch, data->iovs,
					 data->iovcnt, addr, bytes,
					 vbdev_forward_io_simple_cb, ctx);
	} else if (dir == OCF_WRITE) {
		status = spdk_bdev_writev(base->desc, ctx->ch, data->iovs,
					  data->iovcnt, addr, bytes,
					  vbdev_forward_io_simple_cb, ctx);
	}

	if (unlikely(status)) {
		SPDK_ERRLOG("Submission failed with status=%d\n", status);
		spdk_put_io_channel(ctx->ch);
		env_free(ctx);
		ocf_forward_end(token, (status == -ENOMEM) ? -OCF_ERR_NO_MEM : -OCF_ERR_IO);
	}
}

static struct ocf_volume_properties vbdev_volume_props = {
	.name = "SPDK_block_device",
	.io_priv_size = sizeof(struct ocf_io_ctx),
	.volume_priv_size = sizeof(struct vbdev_ocf_base *),
	.caps = {
		.atomic_writes = 0 /* to enable need to have ops->submit_metadata */
	},
	.ops = {
		.open = vbdev_ocf_volume_open,
		.close = vbdev_ocf_volume_close,
		.get_length = vbdev_ocf_volume_get_length,
		.submit_io = vbdev_ocf_volume_submit_io,
		.submit_discard = vbdev_ocf_volume_submit_discard,
		.submit_flush = vbdev_ocf_volume_submit_flush,
		.get_max_io_size = vbdev_ocf_volume_get_max_io_size,
		.submit_metadata = vbdev_ocf_volume_submit_metadata,
		.forward_io = vbdev_forward_io,
		.forward_flush = vbdev_forward_flush,
		.forward_discard = vbdev_forward_discard,
		.forward_io_simple = vbdev_forward_io_simple,
	},
	.io_ops = {
		.set_data = vbdev_ocf_volume_io_set_data,
		.get_data = vbdev_ocf_volume_io_get_data,
	},
};

int
vbdev_ocf_volume_init(void)
{
	return ocf_ctx_register_volume_type(vbdev_ocf_ctx, SPDK_OBJECT, &vbdev_volume_props);
}

void
vbdev_ocf_volume_cleanup(void)
{
	ocf_ctx_unregister_volume_type(vbdev_ocf_ctx, SPDK_OBJECT);
}

SPDK_LOG_REGISTER_COMPONENT(vbdev_ocf_volume)
