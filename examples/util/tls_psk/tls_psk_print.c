/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Intel Corporation.
 *   All rights reserved.
 */

#include "spdk/crc32.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/endian.h"
#include "spdk/hexlify.h"
#include "spdk/log.h"
#include "spdk/nvmf.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/util.h"

#include "spdk_internal/nvme_tcp.h"

#include "openssl/ssl.h"

static char *g_psk_interchange = NULL;
static const char *g_hostnqn = NULL;
static const char *g_subsysnqn = NULL;

static int
derive_and_print_tls_psk(char **psk_interchange, const char *hostnqn, const char *subsysnqn)
{
	char psk_identity[NVMF_PSK_IDENTITY_LEN] = {};
	uint8_t psk_configured[SPDK_TLS_PSK_MAX_LEN] = {};
	uint8_t psk_retained[SPDK_TLS_PSK_MAX_LEN] = {};
	uint8_t tls_psk[SPDK_TLS_PSK_MAX_LEN] = {};
	uint64_t psk_configured_size;
	uint8_t psk_retained_hash;
	uint8_t tls_cipher_suite;
	int rc;

	rc = nvme_tcp_parse_interchange_psk(*psk_interchange, psk_configured, sizeof(psk_configured),
					    &psk_configured_size, &psk_retained_hash);
	if (rc < 0) {
		SPDK_ERRLOG("Failed to parse PSK interchange!\n");
		goto end;
	}

	if (psk_configured_size == SHA256_DIGEST_LENGTH) {
		tls_cipher_suite = NVME_TCP_CIPHER_AES_128_GCM_SHA256;
	} else if (psk_configured_size == SHA384_DIGEST_LENGTH) {
		tls_cipher_suite = NVME_TCP_CIPHER_AES_256_GCM_SHA384;
	} else {
		SPDK_ERRLOG("Unrecognized cipher suite!\n");
		rc = -EINVAL;
		goto end;
	}

	rc = nvme_tcp_generate_psk_identity(psk_identity, NVMF_PSK_IDENTITY_LEN, hostnqn,
					    subsysnqn, tls_cipher_suite);
	if (rc) {
		rc = -EINVAL;
		goto end;
	}

	rc = nvme_tcp_derive_retained_psk(psk_configured, psk_configured_size, hostnqn,
					  psk_retained, SPDK_TLS_PSK_MAX_LEN, psk_retained_hash);
	if (rc < 0) {
		SPDK_ERRLOG("Unable to derive retained PSK!\n");
		goto end;
	}

	rc = nvme_tcp_derive_tls_psk(psk_retained, rc, psk_identity, tls_psk,
				     SSL_MAX_MASTER_KEY_LENGTH, tls_cipher_suite);
	if (rc < 0) {
		SPDK_ERRLOG("Could not generate TLS PSK\n");
		goto end;
	}

	rc = write(1, tls_psk, rc);
	if (rc < 0) {
		SPDK_ERRLOG("Could not print TLS PSK\n");
	}

end:
	spdk_memset_s(tls_psk, sizeof(tls_psk), 0, sizeof(tls_psk));
	spdk_memset_s(psk_configured, sizeof(psk_configured), 0, sizeof(psk_configured));
	spdk_memset_s((void *)*psk_interchange, sizeof(*psk_interchange), 0,
		      sizeof(*psk_interchange));

	return rc;
}

static void
usage(char *program_name)
{
	printf("Use this program to print derived TLS PSK bytes to standard output.\n");
	printf("%s options\n", program_name);
	printf("\t[-k PSK in interchange format]\n");
	printf("\t[-n host NQN]\n");
	printf("\t[-s subsystem NQN]\n");
	printf("\t[-h show this help message]\n");
}

static int
parse_args(int argc, char **argv)
{
	int op;

	while (((op = getopt(argc, argv, "hk:n:s:")) != -1)) {
		switch (op) {
		case 'k':
			g_psk_interchange = optarg;
			break;
		case 'n':
			g_hostnqn = optarg;
			break;
		case 's':
			g_subsysnqn = optarg;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return 1;
		}
	}

	return 0;
}

int
main(int argc, char **argv)
{
	int rc;

	rc = parse_args(argc, argv);
	if (rc != 0) {
		return rc;
	}

	if (!g_psk_interchange || !g_hostnqn || !g_subsysnqn) {
		SPDK_ERRLOG("PSK interchange, host NQN and subsystem NQN are required!\n");
		return 1;
	} else {
		derive_and_print_tls_psk(&g_psk_interchange, g_hostnqn, g_subsysnqn);
	}

	return 0;
}
