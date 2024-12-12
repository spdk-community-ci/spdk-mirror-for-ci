#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (C) 2024 Nutanix Inc. All rights reserved.
#

testdir=$(readlink -f "$(dirname "$0")")
rootdir=$(readlink -f "$testdir/../../..")

set -- "--transport=tcp" "$@"

source "$rootdir/test/common/autotest_common.sh"
source "$rootdir/test/nvmf/common.sh"

nqn=nqn.2016-06.io.spdk:cnode1
pri_hostnqn="nqn.2016-06.io.spdk:host0"
sec_hostnqn="nqn.2016-06.io.spdk:host1"

function cleanup() {
	[[ -n "$initiator_pid" ]] && killprocess $initiator_pid || :
	nvmftestfini
}

function common_target_config() {
	rpc_cmd <<- CONFIG
		framework_start_init
		bdev_malloc_create -b Malloc0 32 512
		nvmf_create_transport $NVMF_TRANSPORT_OPTS --in-capsule-data-size 4096
		nvmf_create_subsystem $nqn -a
		nvmf_subsystem_add_ns $nqn Malloc0
		nvmf_subsystem_add_listener -t tcp -a $NVMF_FIRST_TARGET_IP -s $NVMF_PORT $nqn
	CONFIG
}

function run_spdk_target() {
	tgt_params=("--wait-for-rpc")
	nvmfappstart "${tgt_params[@]}"
	common_target_config
}

function run_nvmf_initiator() {
	"$rootdir/test/app/nvmf_target_tester/nvmf_target_tester" -n -r " \
		traddr:$NVMF_FIRST_TARGET_IP \
		adrfam:IPv4 \
		trsvcid:$NVMF_PORT \
		trtype:$TEST_TRANSPORT \
		hostnqn:$pri_hostnqn" -q $sec_hostnqn &
	initiator_pid=$!
}

# This test only makes sense for the TCP transport
[[ "$TEST_TRANSPORT" != "tcp" ]] && exit 1

nvmftestinit

trap cleanup SIGINT SIGTERM EXIT
run_test "spdk_nvmf_target" run_spdk_target

run_test "nvmf_target_tester" run_nvmf_initiator
wait $initiator_pid

trap - SIGINT SIGTERM EXIT
nvmftestfini
