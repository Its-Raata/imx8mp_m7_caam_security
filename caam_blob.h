/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM BLOB protocol on JR1 — hardware-bound wrap of a plaintext key.
 *
 * CAAM name: red blob (general memory, 16-byte key modifier).
 * This project calls it "black blob": the wrap key is OTPMK/JDKEK, not
 * the Step-01 software constant. Same STORE/LOAD/EXPORT commands;
 * blob bytes are device-bound.
 */
#ifndef CAAM_BLOB_H_
#define CAAM_BLOB_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encrypted key blob (32) + MAC (16). Output size = key_len + this. */
#define CAAM_BLOB_OVERHEAD (48u)

int caam_black_blob_wrap(const uint8_t modifier[16],
                         const uint8_t *key,
                         size_t key_len,
                         uint8_t *out,
                         uint32_t *out_len);

int caam_black_blob_unwrap(const uint8_t modifier[16],
                           const uint8_t *blob,
                           uint32_t blob_len,
                           uint8_t *key,
                           size_t key_len);

/* Wrap then unwrap a 16-byte pattern. 0 = match. */
int caam_blob_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* CAAM_BLOB_H_ */
