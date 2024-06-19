#!/usr/bin/env bash
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Intel Corporation
#  All rights reserved.
#
set -e

# DISKCONF - file containing BDF addresses of disks to format
# DS - logical block size (number of bytes; will be converted to power of 2 value)
# MS - metadatasize (number of bytes)

DISKSCONF=$1
DS=$2
MS=$3

if [[ ! -f $DISKSCONF ]]; then
	echo "No such file: $DISKSCONF" >&2
fi

LBADS=$(printf '%.0f\n' $(bc -l <<< "l($DS)/l(2)"))

for DISKADDR in $(<"$DISKSCONF"); do
	DISK=$(basename $(readlink /dev/disk/by-path/pci-${DISKADDR}-nvme-1))
	message="Requested change of $DISK to LBAF:$LBADS:$MS"

	# This is basically: "find element which .ms and .ds values match what we're looking for and return this element's index".
	# jq gets overcomplicated at some point, but I really prefer a oneliner here rather than parsing JSON with Bash.
	lbaf_index=$(nvme id-ns -o json /dev/$DISK | jq --argjson v "{\"ms\": $MS, \"ds\": $LBADS}" \
		'.lbafs | to_entries | map(select(.value.ms==$v.ms and .value.ds==$v.ds) | .key) | .[0]')

	if [[ $lbaf_index == "null" ]]; then
		message+=" - Requested LBAF not found for this disk."
		echo $message
		exit 1
	fi

	current_lbaf=$(($(nvme id-ns -o json /dev/$DISK | jq '.flbas') & 0xF))
	if (( current_lbaf==lbaf_index )); then
		message+=" - Current LBAF and requested LBAF are the same, skipping."
		echo $message
		continue
	fi

	nvme format /dev/$DISK --lbaf=$lbaf_index -pil=0 --pi=2 --ms=1 --force
	message+=" - formatted."
	echo $message
done
