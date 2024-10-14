#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2016 Intel Corporation
#  Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES
#  All rights reserved.
#

[[ $(uname -s) == FreeBSD ]] && return 0

NVMF_PORT=4420
NVMF_SECOND_PORT=4421
NVMF_THIRD_PORT=4422
NVMF_TCP_IP_ADDRESS="127.0.0.1"
NVMF_TRANSPORT_OPTS=""
NVMF_SERIAL=SPDKISFASTANDAWESOME
NVME_HOSTNQN=$(nvme gen-hostnqn)
NVME_HOSTID=${NVME_HOSTNQN##*:}
NVME_HOST=("--hostnqn=$NVME_HOSTNQN" "--hostid=$NVME_HOSTID")
NVME_CONNECT="nvme connect"
NET_TYPE=${NET_TYPE:-phy-fallback}
NVME_SUBNQN=nqn.2016-06.io.spdk:testnqn

function build_nvmf_app_args() {
	if [ $SPDK_RUN_NON_ROOT -eq 1 ]; then
		# We assume that test script is started from sudo
		NVMF_APP=(sudo -E -u $SUDO_USER "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" "${NVMF_APP[@]}")
	fi
	NVMF_APP+=(-i "$NVMF_APP_SHM_ID" -e 0xFFFF)

	NVMF_APP+=("${NO_HUGE[@]}")

	if [ "$TEST_INTERRUPT_MODE" -eq 1 ]; then
		NVMF_APP+=(--interrupt-mode)
	fi

	if [ -n "$SPDK_HUGE_DIR" ]; then
		NVMF_APP+=(--huge-dir "$SPDK_HUGE_DIR")
	elif [ $SPDK_RUN_NON_ROOT -eq 1 ]; then
		echo "In non-root test mode you have to set SPDK_HUGE_DIR variable." >&2
		echo "For example:" >&2
		echo "sudo mkdir /mnt/spdk_hugetlbfs" >&2
		echo "sudo chown ${SUDO_USER}: /mnt/spdk_hugetlbfs" >&2
		echo "export SPDK_HUGE_DIR=/mnt/spdk_hugetlbfs" >&2
		return 1
	fi
}

source "$rootdir/scripts/common.sh"
source "$rootdir/test/nvmf/setup.sh"

: ${NVMF_APP_SHM_ID="0"}
export NVMF_APP_SHM_ID
build_nvmf_app_args

have_pci_nics=0

function rxe_cfg() {
	"$rootdir/scripts/rxe_cfg_small.sh" "$@"
}

function load_ib_rdma_modules() {
	if [ $(uname) != Linux ]; then
		return 0
	fi

	modprobe ib_cm
	modprobe ib_core
	modprobe ib_umad
	modprobe ib_uverbs
	modprobe iw_cm
	modprobe rdma_cm
	modprobe rdma_ucm
}

function get_rdma_if_list() {
	local net_dev rxe_net_dev rxe_net_devs rdma_devs=()

	mapfile -t rxe_net_devs < <(rxe_cfg rxe-net)

	if ((${#net_devs[@]} == 0)); then
		return 1
	fi

	# Pick only these devices which were found during gather_supported_nvmf_pci_devs() run
	# and overwrite global net_devs[@] with what we found.
	for net_dev in "${net_devs[@]}"; do
		for rxe_net_dev in "${rxe_net_devs[@]}"; do
			if [[ $net_dev == "$rxe_net_dev" ]]; then
				rdma_devs+=("$net_dev")
				continue 2
			fi
		done
	done

	((${#rdma_devs[@]} > 0)) || return 1
	net_devs=("${rdma_devs[@]}")
}

function nvmfcleanup() {
	sync

	if [ "$TEST_TRANSPORT" == "tcp" ] || [ "$TEST_TRANSPORT" == "rdma" ]; then
		set +e
		for i in {1..20}; do
			modprobe -v -r nvme-$TEST_TRANSPORT
			if modprobe -v -r nvme-fabrics; then
				set -e
				return 0
			fi
			sleep 1
		done
		set -e

		# So far unable to remove the kernel modules. Try
		# one more time and let it fail.
		# Allow the transport module to fail for now. See Jim's comment
		# about the nvme-tcp module below.
		modprobe -v -r nvme-$TEST_TRANSPORT || true
		modprobe -v -r nvme-fabrics
	fi
}

function gather_supported_nvmf_pci_devs() {
	# Go through the entire pci bus and gather all ethernet controllers we support for the nvmf tests.
	# Focus on the hardware that's currently being tested by the CI.
	xtrace_disable
	cache_pci_bus_sysfs
	xtrace_restore

	local intel=0x8086 mellanox=0x15b3 pci net_dev

	local -a pci_devs=()
	local -a pci_net_devs=()
	local -A pci_drivers=()

	local -ga net_devs=()
	local -ga e810=()
	local -ga x722=()
	local -ga mlx=()

	# E810-XXV
	e810+=(${pci_bus_cache["$intel:0x1592"]})
	e810+=(${pci_bus_cache["$intel:0x159b"]})
	# X722 10G
	x722+=(${pci_bus_cache["$intel:0x37d2"]})
	# BlueField 3
	mlx+=(${pci_bus_cache["$mellanox:0xa2dc"]})
	# ConnectX-7
	mlx+=(${pci_bus_cache["$mellanox:0x1021"]})
	# BlueField 2
	mlx+=(${pci_bus_cache["$mellanox:0xa2d6"]})
	# ConnectX-6 Dx
	mlx+=(${pci_bus_cache["$mellanox:0x101d"]})
	# ConnectX-6
	mlx+=(${pci_bus_cache["$mellanox:0x101b"]})
	# ConnectX-5
	mlx+=(${pci_bus_cache["$mellanox:0x1017"]})
	mlx+=(${pci_bus_cache["$mellanox:0x1019"]})
	# ConnectX-4
	mlx+=(${pci_bus_cache["$mellanox:0x1015"]})
	mlx+=(${pci_bus_cache["$mellanox:0x1013"]})

	pci_devs+=("${e810[@]}")
	if [[ $TEST_TRANSPORT == rdma ]]; then
		pci_devs+=("${x722[@]}")
		pci_devs+=("${mlx[@]}")
	fi

	# Try to respect what CI wants to test and override pci_devs[]
	if [[ $SPDK_TEST_NVMF_NICS == mlx5 ]]; then
		pci_devs=("${mlx[@]}")
	elif [[ $SPDK_TEST_NVMF_NICS == e810 ]]; then
		pci_devs=("${e810[@]}")
	elif [[ $SPDK_TEST_NVMF_NICS == x722 ]]; then
		pci_devs=("${x722[@]}")
	fi

	if ((${#pci_devs[@]} == 0)); then
		return 1
	fi

	# Load proper kernel modules if necessary
	for pci in "${pci_devs[@]}"; do
		echo "Found $pci (${pci_ids_vendor["$pci"]} - ${pci_ids_device["$pci"]})"
		if [[ ${pci_mod_resolved["$pci"]} == unknown ]]; then
			echo "Unresolved modalias for $pci (${pci_mod_driver["$pci"]}). Driver not installed|builtin?"
			continue
		fi
		if [[ ${pci_bus_driver["$pci"]} == unbound ]]; then
			echo "$pci not bound, needs ${pci_mod_resolved["$pci"]}"
			pci_drivers["${pci_mod_resolved["$pci"]}"]=1
		fi
		if [[ ${pci_ids_device["$pci"]} == "0x1017" ]] \
			|| [[ ${pci_ids_device["$pci"]} == "0x1019" ]] \
			|| [[ $TEST_TRANSPORT == rdma ]]; then
			# Reduce maximum number of queues when connecting with
			# ConnectX-5 NICs. When using host systems with nproc > 64
			# connecting with default options (where default equals to
			# number of host online CPUs) creating all IO queues
			# takes too much time and results in keep-alive timeout.
			# See:
			# https://github.com/spdk/spdk/issues/2772
			# 0x1017 - MT27800 Family ConnectX-5
			# 0x1019 - MT28800 Family ConnectX-5 Ex
			NVME_CONNECT="nvme connect -i 15"
		fi
	done

	if ((${#pci_drivers[@]} > 0)); then
		echo "Loading kernel modules: ${!pci_drivers[*]}"
		modprobe -a "${!pci_drivers[@]}"
	fi

	# E810 cards also need irdma driver to be around.
	if [[ $SPDK_TEST_NVMF_NICS == e810 && $TEST_TRANSPORT == rdma ]]; then
		if [[ -e /sys/module/irdma/parameters/roce_ena ]]; then
			# Our tests don't play well with iWARP protocol. Make sure we use RoCEv2 instead.
			(($(< /sys/module/irdma/parameters/roce_ena) != 1)) && modprobe -r irdma
		fi
		modinfo irdma && modprobe irdma roce_ena=1
	fi > /dev/null

	# All devices detected, kernel modules loaded. Now look under net class to see if there
	# are any net devices bound to the controllers.
	for pci in "${pci_devs[@]}"; do
		pci_net_devs=("/sys/bus/pci/devices/$pci/net/"*)

		# Check if available devices are in proper operational state. If not, remove them from the main list.
		# This check is valid for TCP only since for RDMA we use infiniband which don't rely on actual UP
		# state of the device.
		if [[ $TEST_TRANSPORT == tcp ]]; then
			for net_dev in "${!pci_net_devs[@]}"; do
				[[ $(< "${pci_net_devs[net_dev]}/operstate") == up ]] || unset -v "pci_net_devs[net_dev]"
			done
		fi

		if ((${#pci_net_devs[@]} == 0)); then
			echo "No operational net devices associated with $pci"
			continue
		fi

		pci_net_devs=("${pci_net_devs[@]##*/}")
		echo "Found net devices under $pci: ${pci_net_devs[*]}"
		net_devs+=("${pci_net_devs[@]}")
	done

	if ((${#net_devs[@]} == 0)); then
		return 1
	fi

	if [[ $TEST_TRANSPORT == rdma ]]; then
		get_rdma_if_list
	fi
}

prepare_net_devs() {
	local -g is_hw=no

	remove_target_ns

	[[ $NET_TYPE != virt ]] && gather_supported_nvmf_pci_devs && is_hw=yes

	if [[ $is_hw == yes ]]; then
		if [[ $TEST_TRANSPORT == tcp ]]; then
			nvmf_tcp_init
		elif [[ $TEST_TRANSPORT == rdma ]]; then
			nvmf_rdma_init
		fi
		return 0
	elif [[ $NET_TYPE == phy ]]; then
		echo "ERROR: No supported devices were found, cannot run the $TEST_TRANSPORT test"
		return 1
	elif [[ $NET_TYPE == phy-fallback ]]; then
		echo "WARNING: No supported devices were found, fallback requested for $TEST_TRANSPORT test"
	fi

	# NET_TYPE == virt or phy-fallback
	if [[ $TEST_TRANSPORT == tcp ]]; then
		nvmf_veth_init
		return 0
	fi

	echo "ERROR: virt and fallback setup is not supported for $TEST_TRANSPORT"
	return 1
}

function nvmftestinit() {
	if [ -z $TEST_TRANSPORT ]; then
		echo "transport not specified - use --transport= to specify"
		return 1
	fi

	trap 'nvmftestfini' SIGINT SIGTERM EXIT

	prepare_net_devs

	if [ "$TEST_MODE" == "iso" ]; then
		$rootdir/scripts/setup.sh
	fi

	# TODO: Get rid of this. This should be done when all tests are untangled from a web of
	# transport<->*_IP<->type dependencies. Currently, some tests are driven by existance
	# of specific *IP vars and/or transport type. This is a mess which doesn't allow us to
	# fully drop the old approach.
	nvmf_legacy_env

	NVMF_TRANSPORT_OPTS="-t $TEST_TRANSPORT"
	if [[ "$TEST_TRANSPORT" == "rdma" ]]; then
		NVMF_TRANSPORT_OPTS="$NVMF_TRANSPORT_OPTS --num-shared-buffers 1024"
	elif [[ "$TEST_TRANSPORT" == "tcp" ]]; then
		NVMF_TRANSPORT_OPTS="$NVMF_TRANSPORT_OPTS -o"
	fi

	if [ "$TEST_TRANSPORT" == "tcp" ] || [ "$TEST_TRANSPORT" == "rdma" ]; then
		# currently we run the host/perf test for TCP even on systems without kernel nvme-tcp
		#  support; that's fine since the host/perf test uses the SPDK initiator
		# maybe later we will enforce modprobe to succeed once we have systems in the test pool
		#  with nvme-tcp kernel support - but until then let this pass so we can still run the
		#  host/perf test with the tcp transport
		modprobe nvme-$TEST_TRANSPORT || true
	fi
}

function nvmfappstart() {
	timing_enter start_nvmf_tgt
	"${NVMF_APP[@]}" "$@" &
	nvmfpid=$!
	waitforlisten $nvmfpid
	timing_exit start_nvmf_tgt
	trap 'process_shm --id $NVMF_APP_SHM_ID || :; nvmftestfini' SIGINT SIGTERM EXIT
}

function nvmftestfini() {
	nvmfcleanup || :
	if [ -n "$nvmfpid" ]; then
		killprocess $nvmfpid
	fi
	if [ "$TEST_MODE" == "iso" ]; then
		$rootdir/scripts/setup.sh reset
	fi
	nvmf_fini
}

function nvme_connect() {
	local init_count
	init_count=$(nvme list | wc -l)

	if ! nvme connect "$@"; then return $?; fi

	for i in $(seq 1 10); do
		if [ $(nvme list | wc -l) -gt $init_count ]; then
			return 0
		else
			sleep 1s
		fi
	done
	return 1
}

function get_nvme_devs() {
	local dev _

	while read -r dev _; do
		if [[ $dev == /dev/nvme* ]]; then
			echo "$dev"
		fi
	done < <(nvme list)
}

function gen_nvmf_target_json() {
	local subsystem config=()

	for subsystem in "${@:-1}"; do
		config+=(
			"$(
				cat <<- EOF
					{
					  "params": {
					    "name": "Nvme$subsystem",
					    "trtype": "$TEST_TRANSPORT",
					    "traddr": "$NVMF_FIRST_TARGET_IP",
					    "adrfam": "ipv4",
					    "trsvcid": "$NVMF_PORT",
					    "subnqn": "nqn.2016-06.io.spdk:cnode$subsystem",
					    "hostnqn": "nqn.2016-06.io.spdk:host$subsystem",
					    "hdgst": ${hdgst:-false},
					    "ddgst": ${ddgst:-false}
					  },
					  "method": "bdev_nvme_attach_controller"
					}
				EOF
			)"
		)
	done
	jq . <<- JSON
		{
		  "subsystems": [
		    {
		      "subsystem": "bdev",
		      "config": [
			{
			  "method": "bdev_nvme_set_options",
			  "params": {
				"action_on_timeout": "none",
				"timeout_us": 0,
				"transport_retry_count": 4,
				"arbitration_burst": 0,
				"low_priority_weight": 0,
				"medium_priority_weight": 0,
				"high_priority_weight": 0,
				"nvme_adminq_poll_period_us": 10000,
				"keep_alive_timeout_ms" : 10000,
				"nvme_ioq_poll_period_us": 0,
				"io_queue_requests": 0,
				"delay_cmd_submit": true
			  }
			},
		        $(
			IFS=","
			printf '%s\n' "${config[*]}"
		),
			{
			  "method": "bdev_wait_for_examine"
			}
		      ]
		    }
		  ]
		}
	JSON
}

configure_kernel_target() {
	local kernel_name=$1 kernel_target_ip=$2
	# Keep it global in scope for easier cleanup
	nvmet=/sys/kernel/config/nvmet
	kernel_subsystem=$nvmet/subsystems/$kernel_name
	kernel_namespace=$kernel_subsystem/namespaces/1
	kernel_port=$nvmet/ports/1

	local block nvme

	if [[ ! -e /sys/module/nvmet ]]; then
		modprobe nvmet
	fi

	[[ -e $nvmet ]]

	"$rootdir/scripts/setup.sh" reset

	# Find nvme with an active ns device
	for block in /sys/block/nvme*; do
		[[ -e $block ]] || continue
		is_block_zoned "${block##*/}" && continue
		block_in_use "${block##*/}" || nvme="/dev/${block##*/}"
	done

	[[ -b $nvme ]]

	mkdir "$kernel_subsystem"
	mkdir "$kernel_namespace"
	mkdir "$kernel_port"

	# It allows only %llx value and for some reason kernel swaps the byte order
	# so setting the serial is not very useful here
	# "$kernel_subsystem/attr_serial"
	echo "SPDK-$kernel_name" > "$kernel_subsystem/attr_model"

	echo 1 > "$kernel_subsystem/attr_allow_any_host"
	echo "$nvme" > "$kernel_namespace/device_path"
	echo 1 > "$kernel_namespace/enable"

	echo "$kernel_target_ip" > "$kernel_port/addr_traddr"
	echo "$TEST_TRANSPORT" > "$kernel_port/addr_trtype"
	echo "$NVMF_PORT" > "$kernel_port/addr_trsvcid"
	echo ipv4 > "$kernel_port/addr_adrfam"

	# Enable the listener by linking the port to previously created subsystem
	ln -s "$kernel_subsystem" "$kernel_port/subsystems/"

	# Check if target is available
	nvme discover "${NVME_HOST[@]}" -a "$kernel_target_ip" -t "$TEST_TRANSPORT" -s "$NVMF_PORT"
}

clean_kernel_target() {
	[[ -e $kernel_subsystem ]] || return 0

	echo 0 > "$kernel_namespace/enable"

	rm -f "$kernel_port/subsystems/${kernel_subsystem##*/}"
	rmdir "$kernel_namespace"
	rmdir "$kernel_port"
	rmdir "$kernel_subsystem"

	modules=(/sys/module/nvmet/holders/*)

	modprobe -r "${modules[@]##*/}" nvmet

	# Get back all nvmes to userspace
	"$rootdir/scripts/setup.sh"
}

format_key() {
	local prefix key digest

	prefix="$1" key="$2" digest="$3"
	python - <<- EOF
		import base64, zlib

		crc = zlib.crc32(b"$key").to_bytes(4, byteorder="little")
		b64 = base64.b64encode(b"$key" + crc).decode("utf-8")
		print("$prefix:{:02x}:{}:".format($digest, b64), end="")
	EOF
}

format_interchange_psk() {
	format_key "NVMeTLSkey-1" "$1" "$2"
}

format_dhchap_key() {
	format_key "DHHC-1" "$1" "$2"
}

gen_dhchap_key() {
	local digest len file key
	local -A digests=([null]=0 [sha256]=1 [sha384]=2 [sha512]=3)

	digest="$1" len=$2
	key=$(xxd -p -c0 -l $((len / 2)) /dev/urandom)
	file=$(mktemp -t "spdk.key-$1.XXX")
	format_dhchap_key "$key" "${digests[$1]}" > "$file"
	chmod 0600 "$file"

	echo "$file"
}

uuid2nguid() {
	tr -d - <<< "${1^^}"
}

ipts() { iptables "$@" -m comment --comment "SPDK_NVMF:$*"; }
iptr() { iptables-save | grep -v SPDK_NVMF | iptables-restore; }
