#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Intel Corporation.
#  All rights reserved.
#

SPDK_ROOT_DIR := $(abspath $(CURDIR)/../../..)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

APP = tls_psk_print

C_SRCS := tls_psk_print.c

SPDK_LIB_LIST = util log event sock

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk
