#!/usr/bin/env bash
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Intel Corporation
#  All rights reserved.
#

testdir=$(readlink -f "$(dirname "$0")")
rootdir=$(readlink -f "$testdir/../../../")
source "$rootdir/test/common/autotest_common.sh"
source "$rootdir/test/dd/common.sh"

declare -a xnvme_io=(
	libaio
	io_uring
	io_uring_cmd
)

declare -a libaio=(
	randread
	randwrite
)

declare -a io_uring=(
	"${libaio[@]}"
)

declare -a io_uring_cmd=(
	"${io_uring[@]}"
	unmap
	write_zeroes
)

declare -a libaio_fio=(
	"${libaio[@]}"
)

declare -a io_uring_fio=(
	"${io_uring[@]}"
)

declare -a io_uring_cmd_fio=(
	"${io_uring_fio[@]}"
)

declare -A xnvme_filename=(
	["libaio"]=/dev/nvme0n1
	["io_uring"]=/dev/nvme0n1
	["io_uring_cmd"]=/dev/ng0n1
)

declare -A xnvme_conserve_cpu=(
	["libaio"]=false
	["io_uring"]=false
	["io_uring_cmd"]=true
)

declare -A method_bdev_xnvme_create_0=(
	["name"]=xnvme_bdev
	["filename"]=${xnvme_filename["libaio"]}
	["io_mechanism"]=libaio
	["conserve_cpu"]=${xnvme_conserve_cpu["libaio"]}
)

rpc_xnvme() {
	rpc_cmd framework_get_config bdev \
		| jq -r ".[] | select(.method == \"bdev_xnvme_create\").params.${1:-name}"
}

prep_nvme() {
	"$rootdir/scripts/setup.sh" reset

	# Make sure io_poll gets enabled
	modprobe -r nvme
	modprobe nvme poll_queues=$(nproc)
}

prep_nvme

for dev in "${xnvme_filename[@]}"; do
	[[ -e $dev ]]
done
