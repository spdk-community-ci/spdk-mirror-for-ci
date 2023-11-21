#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2023 Intel Corporation.
#  All rights reserved.
#

SPDK_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
CLIENT_BUILD_DIR = $(SPDK_ROOT_DIR)/build/go/rpc
CLIENT_SRC := $(wildcard $(CURDIR)/*.go $(CURDIR)/client/*.go)
RPC_CALL_GENERATOR_DIR := $(CURDIR)/generators/structs
RPC_CALL_GENERATOR_SRC := $(wildcard $(RPC_CALL_GENERATOR_DIR)/*.go $(RPC_CALL_GENERATOR_DIR)/*.tmpl)
JSON_RPC_CALLS_SCHEMA_PATH := $(SPDK_ROOT_DIR)/schema/schema.json

.PHONY: all structs clean mock-generate

BUILD_FLAGS=-trimpath -mod=readonly -gcflags="all=-spectre=all -N -l" -asmflags="all=-spectre=all" -ldflags="all=-s -w"
CGOFLAGS=$(BUILD_FLAGS) -buildmode=c-shared

all: $(CLIENT_SRC) structs
	$(Q)go build $(CGOFLAGS) -o $(CLIENT_BUILD_DIR)/libspdk_gorpc.so clientIntegration.go

clean:
	$(Q)rm -rf $(CLIENT_BUILD_DIR) $(RPC_CALL_GENERATOR_SCHEMA_DIR)/schema.json

structs: $(RPC_CALL_GENERATOR_SRC)
	$(Q)go build -C $(RPC_CALL_GENERATOR_DIR) $(BUILD_FLAGS) -o $(CLIENT_BUILD_DIR)/generator
	$(Q)$(CLIENT_BUILD_DIR)/generator $(JSON_RPC_CALLS_SCHEMA_PATH) > generators/utils/rpc.go

mock-generate:
	$(Q)mockery --name=IClient --dir client --output mocks --with-expecter --boilerplate-file mocks/boilerplate.txt
