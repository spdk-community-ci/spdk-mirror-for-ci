#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2015 Intel Corporation.
#  Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES
#  All rights reserved.
#

SPDK_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

SO_VER := 5
SO_MINOR := 1

C_SRCS = json_config.c subsystem.c subsystem_rpc.c rpc.c
LIBNAME = init

SPDK_MAP_FILE = $(abspath $(CURDIR)/spdk_init.map)

include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk
