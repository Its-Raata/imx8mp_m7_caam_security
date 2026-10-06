/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM-backed AES-GCM / HMAC for M7.
 * Same call shapes as sw_crypto.h so crypto_service.c can switch engines.
 * Blob wrap lives in caam_blob.c; first ping also runs the blob KAT.
 */
#ifndef CAAM_CRYPTO_H_
#define CAAM_CRYPTO_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring-up + KATs; returns a static tag for PING ("OK-CAAM" or "FAIL-*"). */
const char *caam_crypto_ping_step(void);

/* 1 after ping reached OK-CAAM. */
int caam_crypto_is_ready(void);

int caam_aes128_gcm_encrypt(const uint8_t key[16],
                            const uint8_t *iv, size_t iv_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *pt, size_t pt_len,
                            uint8_t *ct, uint8_t tag[16]);

int caam_aes128_gcm_decrypt(const uint8_t key[16],
                            const uint8_t *iv, size_t iv_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *ct, size_t ct_len,
                            const uint8_t tag[16],
                            uint8_t *pt);

void caam_hmac_sha256(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len,
                      uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif /* CAAM_CRYPTO_H_ */
