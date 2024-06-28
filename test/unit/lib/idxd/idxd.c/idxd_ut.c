/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2023 Intel Corporation.
 *   All rights reserved.
 */

#include "spdk_internal/cunit.h"
#include "spdk_internal/mock.h"
#include "spdk_internal/idxd.h"
#include "common/lib/test_env.c"

#include "idxd/idxd.c"

static void
test_idxd_validate_dif_common_params(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	/* Check all supported combinations of the data block size and metadata size */
	/* ## supported: data-block-size = 512, metadata = 8 */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == 0);

	/* ## supported: data-block-size = 512, metadata = 16 */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_16,
			       METADATA_SIZE_16,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == 0);

	/* ## supported: data-block-size = 4096, metadata = 8 */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4096 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == 0);

	/* ## supported: data-block-size = 4096, metadata = 16 */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4096 + METADATA_SIZE_16,
			       METADATA_SIZE_16,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == 0);

	/* Check byte offset from the start of the whole data buffer */
	/* ## not-supported: data_offset != 0 */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 10, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* Check seed value for guard computation */
	/* ## not-supported: guard_seed != 0 */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 10, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* Check for supported metadata sizes */
	/* ## not-supported: md_size != 8 or md_size != 16 */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4096 + 32,
			       32,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* Check for supported metadata locations */
	/* ## not-supported: md_interleave == false (separated metadata location) */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4096,
			       METADATA_SIZE_16,
			       false,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* Check for supported DIF alignments */
	/* ## not-supported: dif_loc == true (DIF left alignment) */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4096 + METADATA_SIZE_16,
			       METADATA_SIZE_16,
			       true,
			       true,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* Check for supported DIF block sizes */
	/* ## not-supported: data block_size != 512,520,4096,4104 */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + 10,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* Check for supported DIF PI formats */
	/* ## not-supported: DIF PI format == 32 */
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_32;
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4096 + METADATA_SIZE_16,
			       METADATA_SIZE_16,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* ## not-supported: DIF PI format == 64 */
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_64;
	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4096 + METADATA_SIZE_16,
			       METADATA_SIZE_16,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_common_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);
}

static void
test_idxd_validate_dif_check_params(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_validate_dif_check_params(&dif_ctx);
	CU_ASSERT(rc == 0);
}

static void
test_idxd_validate_dif_insert_params(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	/* Check for required DIF flags */
	/* ## supported: Guard, ApplicationTag, ReferenceTag check flags set */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       512 + 8,
			       8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_insert_params(&dif_ctx);
	CU_ASSERT(rc == 0);

	/* ## not-supported: Guard check flag not set */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       512 + 8,
			       8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_APPTAG_CHECK | SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_insert_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* ## not-supported: Application Tag check flag not set */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       512 + 8,
			       8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_insert_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);

	/* ## not-supported: Reference Tag check flag not set */
	rc = spdk_dif_ctx_init(&dif_ctx,
			       512 + 8,
			       8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);
	rc = idxd_validate_dif_insert_params(&dif_ctx);
	CU_ASSERT(rc == -EINVAL);
}

static void
test_idxd_validate_dif_check_buf_align(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_validate_dif_check_buf_align(&dif_ctx, 4 * (512 + 8));
	CU_ASSERT(rc == 0);

	/* The memory buffer length is not a multiple of block size */
	rc = idxd_validate_dif_check_buf_align(&dif_ctx, 4 * (512 + 8) + 10);
	CU_ASSERT(rc == -EINVAL);
}

static void
test_idxd_validate_dif_insert_buf_align(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	rc = spdk_dif_ctx_init(&dif_ctx,
			       512 + 8,
			       8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	/* The memory source and destination buffer length set correctly */
	rc = idxd_validate_dif_insert_buf_align(&dif_ctx, 4 * 512, 4 * 520);
	CU_ASSERT(rc == 0);

	/* The memory source buffer length is not a multiple of data block size */
	rc = idxd_validate_dif_insert_buf_align(&dif_ctx, 4 * 512 + 10, 4 * 520);
	CU_ASSERT(rc == -EINVAL);

	/* The memory destination buffer length is not a multiple of block size */
	rc = idxd_validate_dif_insert_buf_align(&dif_ctx, 4 * 512, 4 * 520 + 10);
	CU_ASSERT(rc == -EINVAL);

	/* The memory source and destination must hold the same number of blocks */
	rc = idxd_validate_dif_insert_buf_align(&dif_ctx, 4 * 512, 5 * 520);
	CU_ASSERT(rc == -EINVAL);
}

static void
test_idxd_validate_dif_strip_buf_align(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	rc = spdk_dif_ctx_init(&dif_ctx,
			       512 + 8,
			       8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	/* The memory source and destination buffer length set correctly */
	rc = idxd_validate_dif_strip_buf_align(&dif_ctx, 4 * 520, 4 * 512);
	CU_ASSERT(rc == 0);

	/* The memory source buffer length is not a multiple of data block size */
	rc = idxd_validate_dif_strip_buf_align(&dif_ctx, 4 * 520 + 10, 4 * 512);
	CU_ASSERT(rc == -EINVAL);

	/* The memory destination buffer length is not a multiple of block size */
	rc = idxd_validate_dif_strip_buf_align(&dif_ctx, 4 * 512, 4 * 520 + 10);
	CU_ASSERT(rc == -EINVAL);

	/* The memory source and destination must hold the same number of blocks */
	rc = idxd_validate_dif_strip_buf_align(&dif_ctx, 4 * 520, 5 * 512);
	CU_ASSERT(rc == -EINVAL);
}

static void
test_idxd_get_dif_flags(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	uint8_t flags = 0;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == 0);
	CU_ASSERT(flags == IDXD_DIF_FLAG_DIF_BLOCK_SIZE_512);

	dif_ctx.guard_interval = 100;
	rc = idxd_get_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == -EINVAL);

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_520 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == 0);
	CU_ASSERT(flags == IDXD_DIF_FLAG_DIF_BLOCK_SIZE_520);

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4096 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == 0);
	CU_ASSERT(flags == IDXD_DIF_FLAG_DIF_BLOCK_SIZE_4096);

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_4104 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK |
			       SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == 0);
	CU_ASSERT(flags == IDXD_DIF_FLAG_DIF_BLOCK_SIZE_4104);
}

static void
test_idxd_get_source_dif_flags(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	uint8_t flags;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       0,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_source_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == 0);
	CU_ASSERT(flags == (IDXD_DIF_SOURCE_FLAG_GUARD_CHECK_DISABLE |
			    IDXD_DIF_SOURCE_FLAG_REF_TAG_CHECK_DISABLE |
			    IDXD_DIF_SOURCE_FLAG_APP_TAG_F_DETECT));

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_source_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == 0);
	CU_ASSERT(flags == (IDXD_DIF_SOURCE_FLAG_APP_TAG_F_DETECT));

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE3,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_REFTAG_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_source_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == 0);
	CU_ASSERT(flags == (IDXD_DIF_SOURCE_FLAG_APP_AND_REF_TAG_F_DETECT));

	dif_ctx.dif_type = 0xF;
	rc = idxd_get_source_dif_flags(&dif_ctx, &flags);
	CU_ASSERT(rc == -EINVAL);
}

static void
test_idxd_get_app_tag_mask(void)
{
	struct spdk_dif_ctx dif_ctx;
	struct spdk_dif_ctx_init_ext_opts dif_opts;
	uint16_t app_tag_mask, app_tag_mask_expected;
	int rc;

	dif_opts.size = SPDK_SIZEOF(&dif_opts, dif_pi_format);
	dif_opts.dif_pi_format = SPDK_DIF_PI_FORMAT_16;

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK,
			       0, 0, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_app_tag_mask(&dif_ctx, &app_tag_mask);
	CU_ASSERT(rc == 0);
	app_tag_mask_expected = 0xFFFF;
	CU_ASSERT(app_tag_mask == app_tag_mask_expected);

	rc = spdk_dif_ctx_init(&dif_ctx,
			       DATA_BLOCK_SIZE_512 + METADATA_SIZE_8,
			       METADATA_SIZE_8,
			       true,
			       false,
			       SPDK_DIF_TYPE1,
			       SPDK_DIF_FLAGS_GUARD_CHECK | SPDK_DIF_FLAGS_APPTAG_CHECK,
			       0, 10, 0, 0, 0, &dif_opts);
	CU_ASSERT(rc == 0);

	rc = idxd_get_app_tag_mask(&dif_ctx, &app_tag_mask);
	CU_ASSERT(rc == 0);
	app_tag_mask_expected = ~dif_ctx.apptag_mask;
	CU_ASSERT(app_tag_mask == app_tag_mask_expected);
}

static void
test_idxd_bioviter_2_iovs(void)
{
	struct iovec iov1[3], iov2[1];
	size_t iovcnt1 = 3, iovcnt2 = 1;
	uint8_t iov_buffer1[4 * 512 + 123];
	uint8_t iov_buffer2[4 * 520];
	uint32_t block_size1 = 512;
	uint32_t block_size2 = 520;
	uint64_t num_blocks;
	void *out1, *out2;
	struct spdk_ioviter iter;

	iov1[0].iov_base = iov_buffer1;
	iov1[0].iov_len = 512;
	iov1[1].iov_base = iov1[0].iov_base + iov1[0].iov_len;
	iov1[1].iov_len = 512;
	iov1[2].iov_base = iov1[1].iov_base + iov1[1].iov_len;
	iov1[2].iov_len = 2 * 512 + 123;

	iov2[0].iov_base = iov_buffer2;
	iov2[0].iov_len = 4 * 520;

	num_blocks = spdk_idxd_bioviter_first(&iter, iov1, iovcnt1, iov2, iovcnt2,
					      block_size1, block_size2, &out1, &out2);
	CU_ASSERT(num_blocks == 1);
	CU_ASSERT(out1 == iov1[0].iov_base);
	CU_ASSERT(out2 == iov2[0].iov_base);

	num_blocks = spdk_idxd_bioviter_next(&iter, &out1, &out2);
	CU_ASSERT(num_blocks == 1);
	CU_ASSERT(out1 == iov1[1].iov_base);
	CU_ASSERT(out2 == iov2[0].iov_base + block_size2);

	num_blocks = spdk_idxd_bioviter_next(&iter, &out1, &out2);
	CU_ASSERT(num_blocks == 2);
	CU_ASSERT(out1 == iov1[2].iov_base);
	CU_ASSERT(out2 == iov2[0].iov_base + block_size2 * 2);

	num_blocks = spdk_idxd_bioviter_next(&iter, &out1, &out2);
	CU_ASSERT(num_blocks == 0);
}

static void
test_idxd_bioviter_3_iovs(void)
{
	struct iovec iov1[3], iov2[1], iov3[2];
	struct iovec *iovs[3];
	size_t iovcnt1 = 3, iovcnt2 = 1, iovcnt3 = 2;
	size_t iovcnts[3];
	uint8_t iov_buffer1[4 * 512 + 123];
	uint8_t iov_buffer2[4 * 520 + 234];
	uint8_t iov_buffer3[5 * 528];
	uint32_t block_size1 = 512;
	uint32_t block_size2 = 520;
	uint32_t block_size3 = 528;
	uint32_t block_sizes[3];
	uint64_t num_blocks;
	void *outs[3];
	struct spdk_ioviter *iter = malloc(SPDK_IOVITER_SIZE(3));

	iov1[0].iov_base = iov_buffer1;
	iov1[0].iov_len = 512;
	iov1[1].iov_base = iov1[0].iov_base + iov1[0].iov_len;
	iov1[1].iov_len = 512;
	iov1[2].iov_base = iov1[1].iov_base + iov1[1].iov_len;
	iov1[2].iov_len = 2 * 512 + 123;

	iov2[0].iov_base = iov_buffer2;
	iov2[0].iov_len = 4 * 520 + 234;

	iov3[0].iov_base = iov_buffer3;
	iov3[0].iov_len = 2 * 528;
	iov3[1].iov_base = iov3[0].iov_base + iov3[0].iov_len;
	iov3[1].iov_len = 3 * 528;

	iovs[0] = iov1;
	iovs[1] = iov2;
	iovs[2] = iov3;

	iovcnts[0] = iovcnt1;
	iovcnts[1] = iovcnt2;
	iovcnts[2] = iovcnt3;

	block_sizes[0] = block_size1;
	block_sizes[1] = block_size2;
	block_sizes[2] = block_size3;

	num_blocks = idxd_bioviter_firstv(iter, 3, iovs, iovcnts,
					  block_sizes, outs);
	CU_ASSERT(num_blocks == 1);
	CU_ASSERT(outs[0] == iov1[0].iov_base);
	CU_ASSERT(outs[1] == iov2[0].iov_base);
	CU_ASSERT(outs[2] == iov3[0].iov_base);

	num_blocks = idxd_bioviter_nextv(iter, outs);
	CU_ASSERT(num_blocks == 1);
	CU_ASSERT(outs[0] == iov1[1].iov_base);
	CU_ASSERT(outs[1] == iov2[0].iov_base + block_size2);
	CU_ASSERT(outs[2] == iov3[0].iov_base + block_size3);

	num_blocks = idxd_bioviter_nextv(iter, outs);
	CU_ASSERT(num_blocks == 2);
	CU_ASSERT(outs[0] == iov1[2].iov_base);
	CU_ASSERT(outs[1] == iov2[0].iov_base + block_size2 * 2);
	CU_ASSERT(outs[2] == iov3[1].iov_base);

	num_blocks = idxd_bioviter_nextv(iter, outs);
	CU_ASSERT(num_blocks == 0);

	free(iter);
}

int
main(int argc, char **argv)
{
	CU_pSuite   suite = NULL;
	unsigned int    num_failures;

	CU_initialize_registry();

	suite = CU_add_suite("idxd", NULL, NULL);

	CU_ADD_TEST(suite, test_idxd_validate_dif_common_params);
	CU_ADD_TEST(suite, test_idxd_validate_dif_check_params);
	CU_ADD_TEST(suite, test_idxd_validate_dif_check_buf_align);
	CU_ADD_TEST(suite, test_idxd_validate_dif_insert_params);
	CU_ADD_TEST(suite, test_idxd_validate_dif_insert_buf_align);
	CU_ADD_TEST(suite, test_idxd_validate_dif_strip_buf_align);
	CU_ADD_TEST(suite, test_idxd_get_dif_flags);
	CU_ADD_TEST(suite, test_idxd_get_source_dif_flags);
	CU_ADD_TEST(suite, test_idxd_get_app_tag_mask);
	CU_ADD_TEST(suite, test_idxd_bioviter_2_iovs);
	CU_ADD_TEST(suite, test_idxd_bioviter_3_iovs);

	num_failures = spdk_ut_run_tests(argc, argv, NULL);
	CU_cleanup_registry();
	return num_failures;
}
