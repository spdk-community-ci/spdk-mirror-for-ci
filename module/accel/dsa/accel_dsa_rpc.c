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

static int
dsa_scan_decode_driver_type(const struct spdk_json_val *val, void *out)
{
	enum accel_dsa_driver_type *driver_type = out;
	bool kernel_mode;
	int rc;

	rc = spdk_json_decode_bool(val, &kernel_mode);
	if (rc == 0) {
		*driver_type = kernel_mode ? DSA_DRIVER_TYPE_KERNEL : DSA_DRIVER_TYPE_USER;
	}

	return rc;
}

static const struct spdk_json_object_decoder rpc_dsa_scan_accel_module_decoder[] = {
	{"config_kernel_mode", offsetof(struct idxd_probe_opts, driver_type), dsa_scan_decode_driver_type, true},
};

static void
rpc_dsa_scan_accel_module(struct spdk_jsonrpc_request *request,
			  const struct spdk_json_val *params)
{
	struct idxd_probe_opts req = {
		.driver_type = DSA_DRIVER_TYPE_ALL
	};
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

	switch (req.driver_type) {
	case DSA_DRIVER_TYPE_KERNEL:
		SPDK_NOTICELOG("Enabled DSA kernel-mode\n");
		break;
	case DSA_DRIVER_TYPE_USER:
		SPDK_NOTICELOG("Enabled DSA user-mode\n");
		break;
	default:
		SPDK_NOTICELOG("Enabled DSA user-mode and kernel-mode\n");
		break;
	}

	spdk_jsonrpc_send_bool_response(request, true);
}
SPDK_RPC_REGISTER("dsa_scan_accel_module", rpc_dsa_scan_accel_module, SPDK_RPC_STARTUP)
SPDK_RPC_REGISTER_ALIAS_DEPRECATED(dsa_scan_accel_module, dsa_scan_accel_engine)
