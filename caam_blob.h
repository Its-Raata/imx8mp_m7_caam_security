/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM black-blob wrap/unwrap for exportable keys (Step 02).
 */
#ifndef CAAM_BLOB_H_
#define CAAM_BLOB_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max bytes of CAAM blob we accept on the wire (fits RPMsg payload). */
#define CAAM_BLOB_MAX_BYTES (400u)

/*
 * Wrap plaintext key material into a CAAM black blob.
 * out_len: in = capacity, out = bytes written.
 * Returns 0 on success.
 */
int caam_black_blob_wrap(const uint8_t *key, size_t key_len, uint8_t *out, uint32_t *out_len);

/*
 * Unwrap CAAM black blob into key buffer.
 * key_len: in = capacity, out = bytes written.
 */
int caam_black_blob_unwrap(const uint8_t *blob, uint32_t blob_len, uint8_t *key, uint16_t *key_len);

#ifdef __cplusplus
}
#endif

#endif /* CAAM_BLOB_H_ */
