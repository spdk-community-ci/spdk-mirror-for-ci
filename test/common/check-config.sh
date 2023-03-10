#!/usr/bin/env bash
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2015 Intel Corporation
#  All rights reserved.
#

rootdir=$(readlink -f $(dirname "$0"))/../..
source "$rootdir/scripts/common.sh"

source /etc/os-release
if [[ -e /proc/sys/kernel/osrelease ]]; then
	kernel=$(< /proc/sys/kernel/osrelease)
fi

all_supported=(
	--enable-asan
	--enable-coverage
	--enable-debug
	--enable-ubsan
	--enable-werror
	--with-avahi
	--with-crypto
	--with-daos
	--with-dpdk-compressdev
	--with-fio
	--with-fuse
	--with-golang
	--with-idxd
	--with-iscsi-initiator
	--with-nvme-cuse
	--with-ocf
	--with-raid5f
	--with-rbd
	--with-rdma
	--with-sma
	--with-ublk
	--with-uring
	--with-usdt
	--with-vbdev-compress
	--with-vfio-user
	--with-vtune
	--with-xnvme
)

--enable-asan() { [[ $(uname -s) == Linux ]]; }
--enable-coverage() { [[ $(uname -s) == Linux ]]; }
--enable-debug() { :; }
--enable-ubsan() {
	# Disabled on FreeBSD; There's no package containing
	# libclang_rt.ubsan_standalone-x86_64.so which is needed for build.
	[[ $(uname -s) == Linux ]]
}
--enable-werror() { :; }
--with-avahi() { [[ $(uname -s) == Linux ]]; }
--with-crypto() {
	# Disabled on FreeBSD
	# --with-crypto build fails because of intel-ipsec-mb
	# build errors:
	# ld: error: undefined symbol: __printf_chk
	# ld: error: undefined symbol: __memcpy_chk
	[[ $(uname -s) == Linux ]]
}
--with-daos() { [[ -f /usr/include/daos.h ]]; }
--with-dpdk-compressdev() {
	nasm_version="$(nasm --version | awk '{print $3}')"
	[[ -f /usr/include/libpmem.h ]] && ge "$(nasm -v | awk '{print $3}')" 2.15
}
--with-fio() { :; }
--with-fuse() {
	# Disabled on FreeBSD; fuse3 dependency libs are available and
	# SPDK builds fine, but binaries fail to execute because of
	# issues with finding fuse shared objects.
	[[ $(uname -s) == Linux ]] \
		&& [[ -d /usr/include/fuse3 || -d /usr/local/include/fuse3 ]]
}
--with-golang() { [[ $SPDK_JSONRPC_GO_CLIENT -eq 1 ]]; }
--with-idxd() {
	case "$(uname -s)" in
		Linux) cat /proc/cpuinfo ;;
		FreeBSD) sysctl hw.model ;;
	esac | grep -q Intel
}
--with-iscsi-initiator() {
	[[ -d /usr/include/iscsi ]]
	[[ $(< /usr/include/iscsi/iscsi.h) =~ "define LIBISCSI_API_VERSION ("([0-9]+)")" ]] \
		&& libiscsi_version=${BASH_REMATCH[1]}
	((libiscsi_version >= 20150621))
}
--with-nvme-cuse() {
	# Disabled on FreeBSD; fuse3 dependency libs are available, but
	# cuse component fails to build because of missing linux/nvme_ioctl.h
	[[ $(uname -s) == Linux ]]
}
--with-ocf() {
	# Disabled on FreeBSD; OCF does not compile because "linux/limits.h"
	# is not available.
	[[ $(uname -s) == Linux ]]
}
--with-raid5f() { :; }
--with-rbd() { [[ -d /usr/include/rbd && -d /usr/include/rados ]]; }
--with-rdma() { [[ -f /usr/include/infiniband/verbs.h ]]; }
--with-sma() {
	# To be deleted, comment for review's sake:
	# --with-sma requires grpcio and grpcio-tools python packages.
	[[ ! $ID == centos ]] && ((VERSION_ID != 7))
}
--with-ublk() {
	[[ -f /usr/include/liburing/io_uring.h ]] && [[ -f /usr/include/linux/ublk_cmd.h ]]
}
--with-uring() {
	ge "$kernel" 5.1
}
--with-usdt() {
	# Disabled on FreeBSD; When using --with-usdt we rely on bpftrace
	# which is unavailable on FreeBSD.
	# TODO: There's dtrace available which might be a candidate to use
	# on non-Linux systems.
	[[ $(uname -s) == Linux ]]
}
--with-vbdev-compress() {
	nasm_version="$(nasm --version | awk '{print $3}')"
	[[ -f /usr/include/libpmem.h ]] && ge "$(nasm -v | awk '{print $3}')" 2.15
}
--with-vfio-user() {
	# Disabled on FreeBSD; Dependency packages equivalents are available
	# (json-c-0.16, cmocka-1.1.5), but vfio-user itself fails to build
	# because of missing linux/pci_regs.h.
	[[ $(uname -s) == Linux ]]
}
# --with-vtune()     { :; } # Need to set custom path to configure.
--with-xnvme() {
	ge "$kernel" 5.1
}

# No args? Sanitize all the options
(($#)) || set -- "${all_supported[@]}"

for f; do [[ $(type -t -- "$f") == func* ]] && "$f" && echo "$f"; done
