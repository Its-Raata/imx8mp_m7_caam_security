/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM black blob backend (Step 02).
 * Black-blob wrap/unwrap lands after AES-GCM on JR0 is proven.
 * Soft blobs in crypto_service.c remain the on-wire format.
 */
#include "caam_blob.h"

#include <string.h>

int caam_black_blob_wrap(const uint8_t *key, size_t key_len, uint8_t *out, uint32_t *out_len)
{
    (void)key;
    (void)key_len;
    (void)out;
    if (out_len != NULL)
    {
        *out_len = 0;
    }
    return -1;
}

int caam_black_blob_unwrap(const uint8_t *blob, uint32_t blob_len, uint8_t *key, uint16_t *key_len)
{
    (void)blob;
    (void)blob_len;
    (void)key;
    if (key_len != NULL)
    {
        *key_len = 0;
    }
    return -1;
}
