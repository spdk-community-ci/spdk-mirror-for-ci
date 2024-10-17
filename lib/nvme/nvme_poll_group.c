/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2020 Intel Corporation.
 *   Copyright (c) 2021 Mellanox Technologies LTD.
 *   Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES.
 *   All rights reserved.
 */

#include "nvme_internal.h"

SPDK_LOG_DEPRECATION_REGISTER(nvme_accel_fn_submit_crc,
			      "spdk_nvme_accel_fn_table.submit_accel_crc32c", "v25.01", 0);

void
spdk_nvme_poll_group_default_opts(struct spdk_nvme_poll_group_opts *opts,
				  size_t opts_size)
{
	if (!opts) {
		SPDK_ERRLOG("opts should not be NULL\n");
		return;
	}

	if (!opts_size) {
		SPDK_ERRLOG("opts_size should not be zero value\n");
		return;
	}

	memset(opts, 0, opts_size);
	opts->opts_size = opts_size;

#define FIELD_OK(field) \
        offsetof(struct spdk_nvme_poll_group_opts, field) + sizeof(opts->field) <= opts_size

#define SET_FIELD(field, value) \
        if (FIELD_OK(field)) { \
                opts->field = value; \
        } \

	SET_FIELD(create_fd_group, false);

#undef FIELD_OK
#undef SET_FIELD
}

static void
nvme_poll_group_opts_copy(const struct spdk_nvme_poll_group_opts *src,
			  struct spdk_nvme_poll_group_opts *dst)
{
	if (!src->opts_size) {
		SPDK_ERRLOG("opts_size should not be zero value\n");
		assert(false);
	}

#define FIELD_OK(field) \
        offsetof(struct spdk_nvme_poll_group_opts, field) + sizeof(src->field) <= src->opts_size

#define SET_FIELD(field) \
        if (FIELD_OK(field)) { \
                dst->field = src->field; \
        } \

	SET_FIELD(create_fd_group);

	dst->opts_size = src->opts_size;

	/* You should not remove this statement, but need to update the assert statement
	 * if you add a new field, and also add a corresponding SET_FIELD statement */
	SPDK_STATIC_ASSERT(sizeof(struct spdk_nvme_poll_group_opts) == 16, "Incorrect size");

#undef FIELD_OK
#undef SET_FIELD
}

struct spdk_nvme_poll_group *
spdk_nvme_poll_group_create(void *ctx, struct spdk_nvme_accel_fn_table *table)
{
	struct spdk_nvme_poll_group_opts opts = {};

	spdk_nvme_poll_group_default_opts(&opts, sizeof(opts));
	opts.create_fd_group = false;

	return spdk_nvme_poll_group_create_ext(ctx, table, &opts);
}

struct spdk_nvme_poll_group *
spdk_nvme_poll_group_create_ext(void *ctx, struct spdk_nvme_accel_fn_table *table,
				struct spdk_nvme_poll_group_opts *opts)
{
	struct spdk_nvme_poll_group *group;
	struct spdk_nvme_poll_group_opts local_opts = {};
	int rc;

	group = calloc(1, sizeof(*group));
	if (group == NULL) {
		return NULL;
	}

	spdk_nvme_poll_group_default_opts(&local_opts, sizeof(local_opts));
	if (opts) {
		nvme_poll_group_opts_copy(opts, &local_opts);
	}

	group->accel_fn_table.table_size = sizeof(struct spdk_nvme_accel_fn_table);
	if (table && table->table_size != 0) {
		group->accel_fn_table.table_size = table->table_size;
#define SET_FIELD(field) \
	if (offsetof(struct spdk_nvme_accel_fn_table, field) + sizeof(table->field) <= table->table_size) { \
		group->accel_fn_table.field = table->field; \
	} \

		SET_FIELD(submit_accel_crc32c);
		SET_FIELD(append_crc32c);
		SET_FIELD(append_copy);
		SET_FIELD(finish_sequence);
		SET_FIELD(reverse_sequence);
		SET_FIELD(abort_sequence);
		/* Do not remove this statement, you should always update this statement when you adding a new field,
		 * and do not forget to add the SET_FIELD statement for your added field. */
		SPDK_STATIC_ASSERT(sizeof(struct spdk_nvme_accel_fn_table) == 56, "Incorrect size");

#undef SET_FIELD
	}

	/* Make sure either all or none of the sequence manipulation callbacks are implemented */
	if ((group->accel_fn_table.finish_sequence && group->accel_fn_table.reverse_sequence &&
	     group->accel_fn_table.abort_sequence) !=
	    (group->accel_fn_table.finish_sequence || group->accel_fn_table.reverse_sequence ||
	     group->accel_fn_table.abort_sequence)) {
		SPDK_ERRLOG("Invalid accel_fn_table configuration: either all or none of the "
			    "sequence callbacks must be provided\n");
		free(group);
		return NULL;
	}

	/* Make sure that sequence callbacks are implemented if append* callbacks are provided */
	if ((group->accel_fn_table.append_crc32c || group->accel_fn_table.append_copy) &&
	    !group->accel_fn_table.finish_sequence) {
		SPDK_ERRLOG("Invalid accel_fn_table configuration: append_crc32c and/or append_copy require sequence "
			    "callbacks to be provided\n");
		free(group);
		return NULL;
	}

	if (group->accel_fn_table.submit_accel_crc32c != NULL) {
		SPDK_LOG_DEPRECATED(nvme_accel_fn_submit_crc);
	}

	if (local_opts.create_fd_group) {
		rc = spdk_fd_group_create(&group->fgrp);
		if (rc) {
			SPDK_ERRLOG("Cannot create fd group for the nvme poll group\n");
			free(group);
			return NULL;
		}
	}

	group->ctx = ctx;
	STAILQ_INIT(&group->tgroups);

	return group;
}

int
spdk_nvme_poll_group_get_fd_group_fd(struct spdk_nvme_poll_group *group)
{
	if (!group->fgrp) {
		SPDK_ERRLOG("No fd group present for the nvme poll group.\n");
		return -EINVAL;
	}

	return spdk_fd_group_get_fd(group->fgrp);
}

struct spdk_nvme_poll_group *
spdk_nvme_qpair_get_optimal_poll_group(struct spdk_nvme_qpair *qpair)
{
	struct spdk_nvme_transport_poll_group *tgroup;

	tgroup = nvme_transport_qpair_get_optimal_poll_group(qpair->transport, qpair);

	if (tgroup == NULL) {
		return NULL;
	}

	return tgroup->group;
}

int
spdk_nvme_poll_group_add(struct spdk_nvme_poll_group *group, struct spdk_nvme_qpair *qpair)
{
	struct spdk_nvme_transport_poll_group *tgroup;
	const struct spdk_nvme_transport *transport;

	if (nvme_qpair_get_state(qpair) != NVME_QPAIR_DISCONNECTED) {
		return -EINVAL;
	}

	STAILQ_FOREACH(tgroup, &group->tgroups, link) {
		if (tgroup->transport == qpair->transport) {
			break;
		}
	}

	/* See if a new transport has been added (dlopen style) and we need to update the poll group */
	if (!tgroup) {
		transport = nvme_get_first_transport();
		while (transport != NULL) {
			if (transport == qpair->transport) {
				tgroup = nvme_transport_poll_group_create(transport);
				if (tgroup == NULL) {
					return -ENOMEM;
				}
				tgroup->group = group;
				STAILQ_INSERT_TAIL(&group->tgroups, tgroup, link);
				break;
			}
			transport = nvme_get_next_transport(transport);
		}
	}

	return tgroup ? nvme_transport_poll_group_add(tgroup, qpair) : -ENODEV;
}

int
spdk_nvme_poll_group_remove(struct spdk_nvme_poll_group *group, struct spdk_nvme_qpair *qpair)
{
	struct spdk_nvme_transport_poll_group *tgroup;

	STAILQ_FOREACH(tgroup, &group->tgroups, link) {
		if (tgroup->transport == qpair->transport) {
			return nvme_transport_poll_group_remove(tgroup, qpair);
		}
	}

	return -ENODEV;
}

static int
nvme_poll_group_add_qpair_fd(struct spdk_nvme_qpair *qpair)
{
	struct spdk_nvme_poll_group *group;
	struct spdk_event_handler_opts opts = {};
	const struct spdk_nvme_transport_id *trid;
	int fd;

	group = qpair->poll_group->group;
	/* This will return 0 if fd group is not present in the poll group
	 * for which this qpair is part of.
	 */
	if (!group->fgrp) {
		return 0;
	}

	if (!qpair->opts.event_cb_fn || !qpair->opts.event_cb_arg) {
		SPDK_ERRLOG("Event callback function and argument not specified\n");
		return -EINVAL;
	}

	spdk_fd_group_get_default_event_handler_opts(&opts, sizeof(opts));

	trid = spdk_nvme_ctrlr_get_transport_id(qpair->ctrlr);
	if (trid->trtype == SPDK_NVME_TRANSPORT_PCIE) {
		opts.fd_type = SPDK_FD_TYPE_VFIO;
	}

	fd = spdk_nvme_ctrlr_qpair_get_fd(qpair);
	if (fd < 0) {
		SPDK_ERRLOG("Cannot get fd for the qpair: %d\n", fd);
		return -EINVAL;
	}

	return SPDK_FD_GROUP_ADD_EXT(group->fgrp, fd, qpair->opts.event_cb_fn,
				     qpair->opts.event_cb_arg, &opts);
}

static int
nvme_poll_group_remove_qpair_fd(struct spdk_nvme_qpair *qpair)
{
	struct spdk_nvme_poll_group *group;
	int fd;

	group = qpair->poll_group->group;
	/* This will return 0 if fd group is not present in the poll group
	 * for which this qpair is part of.
	 */
	if (!group->fgrp) {
		return 0;
	}

	fd = spdk_nvme_ctrlr_qpair_get_fd(qpair);
	if (fd < 0) {
		SPDK_ERRLOG("Cannot get fd for the qpair: %d\n", fd);
		return -EINVAL;
	}

	spdk_fd_group_remove(group->fgrp, fd);

	return 0;
}

int
nvme_poll_group_connect_qpair(struct spdk_nvme_qpair *qpair)
{
	int rc;

	rc = nvme_transport_poll_group_connect_qpair(qpair);

	return rc ? rc : nvme_poll_group_add_qpair_fd(qpair);
}

int
nvme_poll_group_disconnect_qpair(struct spdk_nvme_qpair *qpair)
{
	int rc;

	rc = nvme_poll_group_remove_qpair_fd(qpair);

	return rc ? rc : nvme_transport_poll_group_disconnect_qpair(qpair);
}

int
spdk_nvme_poll_group_wait(void *arg)
{
	struct spdk_nvme_poll_group *group = arg;
	int num_events;
	int timeout = -1;

	num_events = spdk_fd_group_wait(group->fgrp, timeout);

	return num_events;
}

int64_t
spdk_nvme_poll_group_process_completions(struct spdk_nvme_poll_group *group,
		uint32_t completions_per_qpair, spdk_nvme_disconnected_qpair_cb disconnected_qpair_cb)
{
	struct spdk_nvme_transport_poll_group *tgroup;
	int64_t local_completions = 0, error_reason = 0, num_completions = 0;

	if (disconnected_qpair_cb == NULL) {
		return -EINVAL;
	}

	if (spdk_unlikely(group->in_process_completions)) {
		return 0;
	}
	group->in_process_completions = true;

	STAILQ_FOREACH(tgroup, &group->tgroups, link) {
		local_completions = nvme_transport_poll_group_process_completions(tgroup, completions_per_qpair,
				    disconnected_qpair_cb);
		if (local_completions < 0 && error_reason == 0) {
			error_reason = local_completions;
		} else {
			num_completions += local_completions;
			/* Just to be safe */
			assert(num_completions >= 0);
		}
	}
	group->in_process_completions = false;

	return error_reason ? error_reason : num_completions;
}

int
spdk_nvme_poll_group_all_connected(struct spdk_nvme_poll_group *group)
{
	struct spdk_nvme_transport_poll_group *tgroup;
	struct spdk_nvme_qpair *qpair;
	int rc = 0;

	STAILQ_FOREACH(tgroup, &group->tgroups, link) {
		if (!STAILQ_EMPTY(&tgroup->disconnected_qpairs)) {
			/* Treat disconnected qpairs as highest priority for notification.
			 * This means we can just return immediately here.
			 */
			return -EIO;
		}
		STAILQ_FOREACH(qpair, &tgroup->connected_qpairs, poll_group_stailq) {
			if (nvme_qpair_get_state(qpair) < NVME_QPAIR_CONNECTING) {
				return -EIO;
			} else if (nvme_qpair_get_state(qpair) == NVME_QPAIR_CONNECTING) {
				rc = -EAGAIN;
				/* Break so that we can check the remaining transport groups,
				 * in case any of them have a disconnected qpair.
				 */
				break;
			}
		}
	}

	return rc;
}

void *
spdk_nvme_poll_group_get_ctx(struct spdk_nvme_poll_group *group)
{
	return group->ctx;
}

int
spdk_nvme_poll_group_destroy(struct spdk_nvme_poll_group *group)
{
	struct spdk_nvme_transport_poll_group *tgroup, *tmp_tgroup;

	STAILQ_FOREACH_SAFE(tgroup, &group->tgroups, link, tmp_tgroup) {
		STAILQ_REMOVE(&group->tgroups, tgroup, spdk_nvme_transport_poll_group, link);
		if (nvme_transport_poll_group_destroy(tgroup) != 0) {
			STAILQ_INSERT_TAIL(&group->tgroups, tgroup, link);
			return -EBUSY;
		}

	}

	if (group->fgrp) {
		spdk_fd_group_destroy(group->fgrp);
	}

	free(group);

	return 0;
}

int
spdk_nvme_poll_group_get_stats(struct spdk_nvme_poll_group *group,
			       struct spdk_nvme_poll_group_stat **stats)
{
	struct spdk_nvme_transport_poll_group *tgroup;
	struct spdk_nvme_poll_group_stat *result;
	uint32_t transports_count = 0;
	/* Not all transports used by this poll group may support statistics reporting */
	uint32_t reported_stats_count = 0;
	int rc;

	assert(group);
	assert(stats);

	result = calloc(1, sizeof(*result));
	if (!result) {
		SPDK_ERRLOG("Failed to allocate memory for poll group statistics\n");
		return -ENOMEM;
	}

	STAILQ_FOREACH(tgroup, &group->tgroups, link) {
		transports_count++;
	}

	result->transport_stat = calloc(transports_count, sizeof(*result->transport_stat));
	if (!result->transport_stat) {
		SPDK_ERRLOG("Failed to allocate memory for poll group statistics\n");
		free(result);
		return -ENOMEM;
	}

	STAILQ_FOREACH(tgroup, &group->tgroups, link) {
		rc = nvme_transport_poll_group_get_stats(tgroup, &result->transport_stat[reported_stats_count]);
		if (rc == 0) {
			reported_stats_count++;
		}
	}

	if (reported_stats_count == 0) {
		free(result->transport_stat);
		free(result);
		SPDK_DEBUGLOG(nvme, "No transport statistics available\n");
		return -ENOTSUP;
	}

	result->num_transports = reported_stats_count;
	*stats = result;

	return 0;
}

void
spdk_nvme_poll_group_free_stats(struct spdk_nvme_poll_group *group,
				struct spdk_nvme_poll_group_stat *stat)
{
	struct spdk_nvme_transport_poll_group *tgroup;
	uint32_t i;
	uint32_t freed_stats __attribute__((unused)) = 0;

	assert(group);
	assert(stat);

	for (i = 0; i < stat->num_transports; i++) {
		STAILQ_FOREACH(tgroup, &group->tgroups, link) {
			if (nvme_transport_get_trtype(tgroup->transport) == stat->transport_stat[i]->trtype) {
				nvme_transport_poll_group_free_stats(tgroup, stat->transport_stat[i]);
				freed_stats++;
				break;
			}
		}
	}

	assert(freed_stats == stat->num_transports);

	free(stat->transport_stat);
	free(stat);
}
