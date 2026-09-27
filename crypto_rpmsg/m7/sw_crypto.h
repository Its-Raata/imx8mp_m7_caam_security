#ifndef SW_CRYPTO_H_
#define SW_CRYPTO_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int sw_aes128_gcm_encrypt(const uint8_t key[16],
                          const uint8_t *iv, size_t iv_len,
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *pt, size_t pt_len,
                          uint8_t *ct, uint8_t tag[16]);

int sw_aes128_gcm_decrypt(const uint8_t key[16],
                          const uint8_t *iv, size_t iv_len,
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *ct, size_t ct_len,
                          const uint8_t tag[16],
                          uint8_t *pt);

void sw_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

void sw_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *data, size_t data_len,
                    uint8_t out[32]);

/* AES-128-CBC encrypt/decrypt, length must be multiple of 16. */
int sw_aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                          const uint8_t *in, size_t len, uint8_t *out);
int sw_aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                          const uint8_t *in, size_t len, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SW_CRYPTO_H_ */
