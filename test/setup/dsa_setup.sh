#!/usr/bin/env bash
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Intel Corporation
#  All rights reserved.
#

shopt -s nullglob extglob

DSA_DEVICES_PATH="/sys/bus/dsa/devices"
DSA_DEVICES=($(basename -a "$DSA_DEVICES_PATH"/dsa+([0-9])))

verify_accel_tools() {
	if ! command -v accel-config &> /dev/null; then
		echo "Package accel-config missing!" >&2
		return 1
	fi
}

get_device_engines() { printf '%s\n' $DSA_DEVICES_PATH/$1/engine*; }

get_wq() { printf '%s\n' $DSA_DEVICES_PATH/$1/wq*.0; }

configure_dsa_engines() {
	for engine in $(get_device_engines "$1"); do
		accel-config config-engine "$1/$(basename $engine)" --group-id=0
	done
}

configure_dsa_work_queue() {
	accel-config config-wq "$(get_wq $1)" --group-id=0 --mode=dedicated --type=user --priority=1 --wq-size=128 --name=spdk
}

enable_dsa_device_and_wq() {
	accel-config enable-device "$1" || true
	accel-config enable-wq "$(get_wq $1)" || true
}

setup_dsa_device() {
	configure_dsa_engines "$1"
	configure_dsa_work_queue "$1"
	enable_dsa_device_and_wq "$1"
}

setup_all_dsa_devices() {
	disable_all_dsa_engines

	for device_name in "${DSA_DEVICES[@]}"; do
		setup_dsa_device "$device_name"
	done

	verify_accel_config
}

verify_accel_config() {
	accel-config list
}

disable_dsa_device_and_wq() {
	accel-config disable-wq "$(get_wq $1)" || true
	accel-config disable-device "$1" || true
}

disable_all_dsa_engines() {
	for device_name in "${DSA_DEVICES[@]}"; do
		disable_dsa_device_and_wq "$device_name"
	done
}

manage_pci_dsa_devices() {
	allow_dsa_devices

	if [[ -z $PCI_ALLOWED ]]; then
		echo "No DSA/IAA devices found, exiting."
		exit 1
	fi

	"$rootdir/scripts/setup.sh" "$1"
}

allow_dsa_devices() {
	xtrace_disable_per_cmd cache_pci_bus
	local dsa_devices iaa_devices

	dsa_devices=(${pci_bus_cache["0x8086:0x0b25"]})
	iaa_devices=(${pci_bus_cache["0x8086:0x0cfe"]})

	export PCI_ALLOWED="${dsa_devices[*]} ${iaa_devices[*]}"
}
