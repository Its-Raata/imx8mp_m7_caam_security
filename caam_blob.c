/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM black blob backend (Step 02).
 * Hardware path requires M7_CAAM_HW + fsl_caam (see docs/step-02-caam.md).
 */
#include "caam_blob.h"

#include <string.h>

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
#include "fsl_caam.h"
#include "caam_imx8mp_device.h"

#define CAAM_KEYMOD_SIZE (16u)
/* SEC4 black blob overhead is typically ~32–48 bytes beyond plaintext. */
#define CAAM_BLOB_OVERHEAD (48u)

static caam_handle_t s_blob_handle = {.jobRing = kCAAM_JobRing0};
#endif

int caam_black_blob_wrap(const uint8_t *key, size_t key_len, uint8_t *out, uint32_t *out_len)
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    uint8_t keymod[CAAM_KEYMOD_SIZE];
    uint8_t raw_blob[CAAM_BLOB_MAX_BYTES];
    uint32_t need;
    size_t i;
    status_t st;

    if ((key == NULL) || (out == NULL) || (out_len == NULL) || (key_len == 0u) || (key_len > 64u))
    {
        return -1;
    }

    need = CAAM_KEYMOD_SIZE + (uint32_t)key_len + CAAM_BLOB_OVERHEAD;
    if (need > *out_len || need > CAAM_BLOB_MAX_BYTES)
    {
        return -1;
    }

    for (i = 0; i < CAAM_KEYMOD_SIZE; i++)
    {
        keymod[i] = (uint8_t)(0xA5u ^ (uint8_t)i ^ (uint8_t)key_len);
    }

    memset(raw_blob, 0, sizeof(raw_blob));
    st = CAAM_BlackBlob_Encapsule(CAAM, &s_blob_handle, keymod, CAAM_KEYMOD_SIZE, key, key_len, raw_blob,
                                  kCAAM_Descriptor_Type_Kek_Kek);
    if (st != kStatus_Success)
    {
        return -1;
    }

    memcpy(out, keymod, CAAM_KEYMOD_SIZE);
    memcpy(out + CAAM_KEYMOD_SIZE, raw_blob, (size_t)key_len + CAAM_BLOB_OVERHEAD);
    *out_len = need;
    return 0;
#else
    (void)key;
    (void)key_len;
    (void)out;
    if (out_len != NULL)
    {
        *out_len = 0;
    }
    return -1;
#endif
}

int caam_black_blob_unwrap(const uint8_t *blob, uint32_t blob_len, uint8_t *key, uint16_t *key_len)
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    uint8_t plain[64];
    uint16_t expect_len;
    status_t st;

    if ((blob == NULL) || (key == NULL) || (key_len == NULL) || (blob_len <= CAAM_KEYMOD_SIZE))
    {
        return -1;
    }

    expect_len = *key_len;
    if ((expect_len == 0u) || (expect_len > 64u))
    {
        expect_len = 16u;
    }
    if (blob_len < CAAM_KEYMOD_SIZE + expect_len + CAAM_BLOB_OVERHEAD)
    {
        return -1;
    }

    memset(plain, 0, sizeof(plain));
    st = CAAM_BlackBlob_Decapsule(CAAM, &s_blob_handle, blob, CAAM_KEYMOD_SIZE, blob + CAAM_KEYMOD_SIZE, plain,
                                  expect_len, kCAAM_Descriptor_Type_Kek_Kek);
    if (st != kStatus_Success)
    {
        return -1;
    }
    memcpy(key, plain, expect_len);
    *key_len = expect_len;
    memset(plain, 0, sizeof(plain));
    return 0;
#else
    (void)blob;
    (void)blob_len;
    (void)key;
    if (key_len != NULL)
    {
        *key_len = 0;
    }
    return -1;
#endif
}
