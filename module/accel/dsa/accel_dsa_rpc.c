/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2022 Intel Corporation.
 *   All rights reserved.
 */

#include "accel_dsa.h"

#include "spdk/rpc.h"
#include "spdk/util.h"
#include "spdk/event.h"
#include "spdk/stdinc.h"
#include "spdk/env.h"
#include "spdk/string.h"

static const struct spdk_json_object_decoder rpc_dsa_scan_accel_module_decoder[] = {
	{"config_kernel_mode", offsetof(struct idxd_probe_opts, kernel_mode), spdk_json_decode_bool, true},
};

static void
rpc_dsa_scan_accel_module(struct spdk_jsonrpc_request *request,
			  const struct spdk_json_val *params)
{
	struct idxd_probe_opts req = {};
	int rc;

	if (params != NULL) {
		if (spdk_json_decode_object(params, rpc_dsa_scan_accel_module_decoder,
					    SPDK_COUNTOF(rpc_dsa_scan_accel_module_decoder),
					    &req)) {
			SPDK_ERRLOG("spdk_json_decode_object() failed\n");
			spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
							 "Invalid parameters");
			return;
		}
	}

	rc = accel_dsa_enable_probe(&req);
	if (rc != 0) {
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		return;
	}

	if (req.kernel_mode) {
		SPDK_NOTICELOG("Enabled DSA kernel-mode\n");
	} else {
		SPDK_NOTICELOG("Enabled DSA user-mode\n");
	}

	spdk_jsonrpc_send_bool_response(request, true);
}
SPDK_RPC_REGISTER("dsa_scan_accel_module", rpc_dsa_scan_accel_module, SPDK_RPC_STARTUP)
SPDK_RPC_REGISTER_ALIAS_DEPRECATED(dsa_scan_accel_module, dsa_scan_accel_engine)
