#!/usr/bin/env bash
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2014-2024 Beijing Memblaze Technology Co., Ltd.
#  All rights reserved.
#
testdir=$(readlink -f $(dirname $0))
rootdir=$(readlink -f $testdir/../..)
source $rootdir/scripts/common.sh
source $rootdir/test/common/autotest_common.sh

rpc_py=$rootdir/scripts/rpc.py

bdf=$(get_first_nvme_bdf)

$SPDK_BIN_DIR/spdk_tgt -m 0x3 &
spdk_tgt_pid=$!
trap 'kill -9 ${spdk_tgt_pid}; exit 1' SIGINT SIGTERM EXIT

waitforlisten $spdk_tgt_pid

$rpc_py bdev_nvme_attach_controller -b Nvme0 -t PCIe -a ${bdf}

# 1) Test bdev_nvme_fw_slot_info RPC
$rpc_py bdev_nvme_fw_slot_info -c Nvme0

# 2) Test bdev_nvme_admin_passthru RPC
#    NOTE: We use identify controller to verify the bdev_nvme_admin_passthru interface.
#          Admin command identify opcode 06h
$rpc_py bdev_nvme_admin_passthru -c Nvme0 --opcode=0x06 --cdw10=0x01 --data-length=4096 --read

# 3) Test bdev_nvme_fw_download RPC
#    NOTE: We don't want to do real firmware download on CI

# Make sure that used firmware file doesn't exist
if [ -f non_existing_file ]; then
	exit 1
fi

# a) Try to download firmware from non existing file
$rpc_py bdev_nvme_fw_download -c Nvme0 -f non_existing_file

# 4) Test bdev_nvme_fw_commit RPC
#    NOTE: We verify the bdev_nvme_fw_commit interface with 'Commit Action=2 Firmeare Slot=1'.
$rpc_py bdev_nvme_fw_commit -c Nvme0 -a 2 -s 1

# 5) Displays the status of step 4
$rpc_py bdev_nvme_fw_slot_info -c Nvme0

$rpc_py bdev_nvme_detach_controller Nvme0

trap - SIGINT SIGTERM EXIT
killprocess $spdk_tgt_pid
