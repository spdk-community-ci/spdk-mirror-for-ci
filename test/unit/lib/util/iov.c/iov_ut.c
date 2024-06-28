/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2019 Intel Corporation.
 *   All rights reserved.
 */

#include "spdk/stdinc.h"

#include "spdk_internal/cunit.h"

#include "util/iov.c"

static int
_check_val(void *buf, size_t len, uint8_t val)
{
	size_t i;
	uint8_t *data = buf;

	for (i = 0; i < len; i++) {
		if (data[i] != val) {
			return -1;
		}
	}

	return 0;
}

static void
test_single_iov(void)
{
	struct iovec siov[1];
	struct iovec diov[1];
	uint8_t sdata[64];
	uint8_t ddata[64];
	ssize_t rc;

	/* Simplest cases- 1 element in each iovec. */

	/* Same size. */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	siov[0].iov_base = sdata;
	siov[0].iov_len = sizeof(sdata);
	diov[0].iov_base = ddata;
	diov[0].iov_len = sizeof(ddata);

	rc = spdk_iovcpy(siov, 1, diov, 1);
	CU_ASSERT(rc == sizeof(sdata));
	CU_ASSERT(_check_val(ddata, 64, 1) == 0);

	/* Source smaller than dest */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	siov[0].iov_base = sdata;
	siov[0].iov_len = 48;
	diov[0].iov_base = ddata;
	diov[0].iov_len = sizeof(ddata);

	rc = spdk_iovcpy(siov, 1, diov, 1);
	CU_ASSERT(rc == 48);
	CU_ASSERT(_check_val(ddata, 48, 1) == 0);
	CU_ASSERT(_check_val(&ddata[48], 16, 0) == 0);

	/* Dest smaller than source */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	siov[0].iov_base = sdata;
	siov[0].iov_len = sizeof(sdata);
	diov[0].iov_base = ddata;
	diov[0].iov_len = 48;

	rc = spdk_iovcpy(siov, 1, diov, 1);
	CU_ASSERT(rc == 48);
	CU_ASSERT(_check_val(ddata, 48, 1) == 0);
	CU_ASSERT(_check_val(&ddata[48], 16, 0) == 0);
}

static void
test_simple_iov(void)
{
	struct iovec siov[4];
	struct iovec diov[4];
	uint8_t sdata[64];
	uint8_t ddata[64];
	ssize_t rc;
	int i;

	/* Simple cases with 4 iov elements */

	/* Same size. */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	for (i = 0; i < 4; i++) {
		siov[i].iov_base = sdata + (16 * i);
		siov[i].iov_len = 16;
		diov[i].iov_base = ddata + (16 * i);
		diov[i].iov_len = 16;
	}

	rc = spdk_iovcpy(siov, 4, diov, 4);
	CU_ASSERT(rc == sizeof(sdata));
	CU_ASSERT(_check_val(ddata, 64, 1) == 0);

	/* Source smaller than dest */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	for (i = 0; i < 4; i++) {
		siov[i].iov_base = sdata + (8 * i);
		siov[i].iov_len = 8;
		diov[i].iov_base = ddata + (16 * i);
		diov[i].iov_len = 16;
	}

	rc = spdk_iovcpy(siov, 4, diov, 4);
	CU_ASSERT(rc == 32);
	CU_ASSERT(_check_val(ddata, 32, 1) == 0);
	CU_ASSERT(_check_val(&ddata[32], 32, 0) == 0);

	/* Dest smaller than source */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	for (i = 0; i < 4; i++) {
		siov[i].iov_base = sdata + (16 * i);
		siov[i].iov_len = 16;
		diov[i].iov_base = ddata + (8 * i);
		diov[i].iov_len = 8;
	}

	rc = spdk_iovcpy(siov, 4, diov, 4);
	CU_ASSERT(rc == 32);
	CU_ASSERT(_check_val(ddata, 32, 1) == 0);
	CU_ASSERT(_check_val(&ddata[32], 32, 0) == 0);
}

static void
test_complex_iov(void)
{
	struct iovec siov[4];
	struct iovec diov[4];
	uint8_t sdata[64];
	uint8_t ddata[64];
	ssize_t rc;
	int i;

	/* More source elements */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	for (i = 0; i < 4; i++) {
		siov[i].iov_base = sdata + (16 * i);
		siov[i].iov_len = 16;
	}
	diov[0].iov_base = ddata;
	diov[0].iov_len = sizeof(ddata);

	rc = spdk_iovcpy(siov, 4, diov, 1);
	CU_ASSERT(rc == sizeof(sdata));
	CU_ASSERT(_check_val(ddata, 64, 1) == 0);

	/* More dest elements */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	for (i = 0; i < 4; i++) {
		diov[i].iov_base = ddata + (16 * i);
		diov[i].iov_len = 16;
	}
	siov[0].iov_base = sdata;
	siov[0].iov_len = sizeof(sdata);

	rc = spdk_iovcpy(siov, 1, diov, 4);
	CU_ASSERT(rc == sizeof(sdata));
	CU_ASSERT(_check_val(ddata, 64, 1) == 0);

	/* Build one by hand that's really terrible */
	memset(sdata, 1, sizeof(sdata));
	memset(ddata, 0, sizeof(ddata));
	siov[0].iov_base = sdata;
	siov[0].iov_len = 1;
	siov[1].iov_base = siov[0].iov_base + siov[0].iov_len;
	siov[1].iov_len = 13;
	siov[2].iov_base = siov[1].iov_base + siov[1].iov_len;
	siov[2].iov_len = 6;
	siov[3].iov_base = siov[2].iov_base + siov[2].iov_len;
	siov[3].iov_len = 44;

	diov[0].iov_base = ddata;
	diov[0].iov_len = 31;
	diov[1].iov_base = diov[0].iov_base + diov[0].iov_len;
	diov[1].iov_len = 9;
	diov[2].iov_base = diov[1].iov_base + diov[1].iov_len;
	diov[2].iov_len = 1;
	diov[3].iov_base = diov[2].iov_base + diov[2].iov_len;
	diov[3].iov_len = 23;

	rc = spdk_iovcpy(siov, 4, diov, 4);
	CU_ASSERT(rc == 64);
	CU_ASSERT(_check_val(ddata, 64, 1) == 0);
}

static void
test_iovs_to_buf(void)
{
	struct iovec iov[4];
	uint8_t sdata[64];
	uint8_t ddata[64];

	memset(&sdata, 1, sizeof(sdata));
	memset(&ddata, 6, sizeof(ddata));

	iov[0].iov_base = sdata;
	iov[0].iov_len = 3;
	iov[1].iov_base = iov[0].iov_base + iov[0].iov_len;
	iov[1].iov_len = 11;
	iov[2].iov_base = iov[1].iov_base + iov[1].iov_len;
	iov[2].iov_len = 21;
	iov[3].iov_base = iov[2].iov_base + iov[2].iov_len;
	iov[3].iov_len = 29;

	spdk_copy_iovs_to_buf(ddata, 64, iov, 4);
	CU_ASSERT(_check_val(ddata, 64, 1) == 0);
}

static void
test_buf_to_iovs(void)
{
	struct iovec iov[4];
	uint8_t sdata[64];
	uint8_t ddata[64];
	uint8_t iov_buffer[64];

	memset(&sdata, 7, sizeof(sdata));
	memset(&ddata, 4, sizeof(ddata));
	memset(&iov_buffer, 1, sizeof(iov_buffer));

	iov[0].iov_base = iov_buffer;
	iov[0].iov_len = 5;
	iov[1].iov_base = iov[0].iov_base + iov[0].iov_len;
	iov[1].iov_len = 15;
	iov[2].iov_base = iov[1].iov_base + iov[1].iov_len;
	iov[2].iov_len = 21;
	iov[3].iov_base = iov[2].iov_base + iov[2].iov_len;
	iov[3].iov_len = 23;

	spdk_copy_buf_to_iovs(iov, 4, sdata, 64);
	spdk_copy_iovs_to_buf(ddata, 64, iov, 4);

	CU_ASSERT(_check_val(ddata, 64, 7) == 0);
}

static void
test_memset(void)
{
	struct iovec iov[4];
	uint8_t iov_buffer[64];

	memset(&iov_buffer, 1, sizeof(iov_buffer));

	iov[0].iov_base = iov_buffer;
	iov[0].iov_len = 5;
	iov[1].iov_base = iov[0].iov_base + iov[0].iov_len;
	iov[1].iov_len = 15;
	iov[2].iov_base = iov[1].iov_base + iov[1].iov_len;
	iov[2].iov_len = 21;
	iov[3].iov_base = iov[2].iov_base + iov[2].iov_len;
	iov[3].iov_len = 23;

	spdk_iov_memset(iov, 4, 0);

	CU_ASSERT(_check_val(iov_buffer, 64, 0) == 0);
}

static void
test_iov_one(void)
{
	struct iovec iov = { 0 };
	int iovcnt;
	char buf[4];

	SPDK_IOV_ONE(&iov, &iovcnt, buf, sizeof(buf));

	CU_ASSERT(iov.iov_base == buf);
	CU_ASSERT(iov.iov_len == sizeof(buf));
	CU_ASSERT(iovcnt == 1);
}

static void
test_iov_xfer(void)
{
	struct spdk_iov_xfer ix;
	uint8_t data[64] = { 0 };
	uint8_t iov_buffer[64];
	struct iovec iov[4];
	size_t i;

	for (i = 0; i < sizeof(iov_buffer); i++) {
		iov_buffer[i] = i;
	}

	iov[0].iov_base = iov_buffer;
	iov[0].iov_len = 5;
	iov[1].iov_base = iov[0].iov_base + iov[0].iov_len;
	iov[1].iov_len = 15;
	iov[2].iov_base = iov[1].iov_base + iov[1].iov_len;
	iov[2].iov_len = 21;
	iov[3].iov_base = iov[2].iov_base + iov[2].iov_len;
	iov[3].iov_len = 23;

	spdk_iov_xfer_init(&ix, iov, 4);

	spdk_iov_xfer_to_buf(&ix, data, 8);
	spdk_iov_xfer_to_buf(&ix, data + 8, 56);

	for (i = 0; i < sizeof(data); i++) {
		CU_ASSERT(data[i] == i);
	}

	for (i = 0; i < sizeof(data); i++) {
		data[i] = sizeof(data) - i;
	}

	spdk_iov_xfer_init(&ix, iov, 4);

	spdk_iov_xfer_from_buf(&ix, data, 5);
	spdk_iov_xfer_from_buf(&ix, data + 5, 3);
	spdk_iov_xfer_from_buf(&ix, data + 8, 56);

	for (i = 0; i < sizeof(iov_buffer); i++) {
		CU_ASSERT(iov_buffer[i] == sizeof(iov_buffer) - i);
	}
}

static void
test_ioviter_block_2_iovs(void)
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

	num_blocks = spdk_bioviter_first(&iter, iov1, iovcnt1, iov2, iovcnt2,
					 block_size1, block_size2, &out1, &out2);
	CU_ASSERT(num_blocks == 1);
	CU_ASSERT(out1 == iov1[0].iov_base);
	CU_ASSERT(out2 == iov2[0].iov_base);

	num_blocks = spdk_ioviter_next(&iter, &out1, &out2);
	CU_ASSERT(num_blocks == 1);
	CU_ASSERT(out1 == iov1[1].iov_base);
	CU_ASSERT(out2 == iov2[0].iov_base + block_size2);

	num_blocks = spdk_ioviter_next(&iter, &out1, &out2);
	CU_ASSERT(num_blocks == 2);
	CU_ASSERT(out1 == iov1[2].iov_base);
	CU_ASSERT(out2 == iov2[0].iov_base + block_size2 * 2);

	num_blocks = spdk_ioviter_next(&iter, &out1, &out2);
	CU_ASSERT(num_blocks == 0);
}

static void
test_ioviter_block_3_iovs(void)
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

	num_blocks = spdk_bioviter_firstv(iter, 3, iovs, iovcnts,
					  block_sizes, outs);
	CU_ASSERT(num_blocks == 1);
	CU_ASSERT(outs[0] == iov1[0].iov_base);
	CU_ASSERT(outs[1] == iov2[0].iov_base);
	CU_ASSERT(outs[2] == iov3[0].iov_base);

	num_blocks = spdk_ioviter_nextv(iter, outs);
	CU_ASSERT(num_blocks == 1);
	CU_ASSERT(outs[0] == iov1[1].iov_base);
	CU_ASSERT(outs[1] == iov2[0].iov_base + block_size2);
	CU_ASSERT(outs[2] == iov3[0].iov_base + block_size3);

	num_blocks = spdk_ioviter_nextv(iter, outs);
	CU_ASSERT(num_blocks == 2);
	CU_ASSERT(outs[0] == iov1[2].iov_base);
	CU_ASSERT(outs[1] == iov2[0].iov_base + block_size2 * 2);
	CU_ASSERT(outs[2] == iov3[1].iov_base);

	num_blocks = spdk_ioviter_nextv(iter, outs);
	CU_ASSERT(num_blocks == 0);

	free(iter);
}

int
main(int argc, char **argv)
{
	CU_pSuite	suite = NULL;
	unsigned int	num_failures;

	CU_initialize_registry();

	suite = CU_add_suite("iov", NULL, NULL);

	CU_ADD_TEST(suite, test_single_iov);
	CU_ADD_TEST(suite, test_simple_iov);
	CU_ADD_TEST(suite, test_complex_iov);
	CU_ADD_TEST(suite, test_iovs_to_buf);
	CU_ADD_TEST(suite, test_buf_to_iovs);
	CU_ADD_TEST(suite, test_memset);
	CU_ADD_TEST(suite, test_iov_one);
	CU_ADD_TEST(suite, test_iov_xfer);
	CU_ADD_TEST(suite, test_ioviter_block_2_iovs);
	CU_ADD_TEST(suite, test_ioviter_block_3_iovs);


	num_failures = spdk_ut_run_tests(argc, argv, NULL);

	CU_cleanup_registry();

	return num_failures;
}
