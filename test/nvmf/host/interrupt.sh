#!/usr/bin/env bash
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Samsung Electronics Co., Ltd.
#  All rights reserved.
#
testdir=$(readlink -f $(dirname $0))
rootdir=$(readlink -f $testdir/../../..)
source $rootdir/test/common/autotest_common.sh
source $rootdir/test/nvmf/common.sh
source $rootdir/test/interrupt/common.sh

rpc_py="$rootdir/scripts/rpc.py"

# Interrupt mode
$rootdir/build/examples/bdevperf -z -q 1 -o 65536 -t 10 -w read -m 0x1 --interrupt-mode &
bdevperf_pid=$!

waitforlisten $bdevperf_pid
trap 'killprocess $bdevperf_pid; exit 1' SIGINT SIGTERM EXIT

$rootdir/scripts/gen_nvme.sh | $rpc_py load_subsystem_config
local_nvme_trid=$($rpc_py framework_get_config bdev | jq -r '.[].params | select(.name=="Nvme0").traddr')

# Check for locally attached nvme devices else exit
if [ -n "$local_nvme_trid" ]; then
	$rootdir/examples/bdev/bdevperf/bdevperf.py perform_tests &
	sleep 1
	intr_cpu_util=$(BUSY_THRESHOLD=50 reactor_cpu_rate $bdevperf_pid 0 8 "intr")
else
	trap - SIGINT SIGTERM EXIT
	killprocess $bdevperf_pid
	exit 0
fi

trap - SIGINT SIGTERM EXIT
killprocess $bdevperf_pid

# Poll mode
$rootdir/build/examples/bdevperf -z -q 1 -o 65536 -t 10 -w read -m 0x1 &
bdevperf_pid=$!

waitforlisten $bdevperf_pid
trap 'killprocess $bdevperf_pid; exit 1' SIGINT SIGTERM EXIT

$rootdir/scripts/gen_nvme.sh | $rpc_py load_subsystem_config
local_nvme_trid=$($rpc_py framework_get_config bdev | jq -r '.[].params | select(.name=="Nvme0").traddr')

$rootdir/examples/bdev/bdevperf/bdevperf.py perform_tests &
sleep 1
poll_cpu_util=$(BUSY_THRESHOLD=95 reactor_cpu_rate $bdevperf_pid 0 8 "poll")

trap - SIGINT SIGTERM EXIT
killprocess $bdevperf_pid

echo "Poll-mode cpu utilization ${poll_cpu_util}"
echo "Interrupt cpu utilization ${intr_cpu_util}"
