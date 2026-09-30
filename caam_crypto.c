/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM crypto backend.
 *
 * MIMX8ML8 MCUXpresso device headers do not yet export CAAM_Type / CAAM_BASE
 * (unlike RT1170). Until that platform shim lands, this file compiles a clear
 * "not ready" path so the USE_CAAM switch and call sites can be reviewed.
 *
 * Enable hardware: define M7_CAAM_HW=1 and provide caam_imx8mp_device.h + link
 * fsl_caam.c (see docs/step-02-caam.md).
 */
#include "caam_crypto.h"

#include <string.h>

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
#include "fsl_caam.h"
#include "caam_imx8mp_device.h"
#endif

static uint8_t s_ready;

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
static caam_handle_t s_handle;
static caam_job_ring_interface_t s_jr0;
#endif

int caam_crypto_init(void)
{
    s_ready = 0;

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    caam_config_t cfg;
    status_t st;

    CAAM_GetDefaultConfig(&cfg);
    cfg.jobRingInterface[0] = &s_jr0;
    cfg.jobRingInterface[1] = NULL;
    cfg.jobRingInterface[2] = NULL;
    cfg.jobRingInterface[3] = NULL;

    st = CAAM_Init(CAAM, &cfg);
    if (st != kStatus_Success)
    {
        return -1;
    }

    s_handle.jobRing = kCAAM_JobRing0;
    s_handle.callback.JobCompleted = NULL;
    s_handle.userData              = NULL;
    s_ready                        = 1;
    return 0;
#else
    /* Scaffold only — hardware path not linked yet. */
    return -1;
#endif
}

int caam_aes128_gcm_encrypt(const uint8_t key[16],
                            const uint8_t *iv, size_t iv_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *pt, size_t pt_len,
                            uint8_t *ct, uint8_t tag[16])
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    status_t st;
    if (!s_ready || (key == NULL) || (iv == NULL) || (iv_len != 12u) || (ct == NULL) || (tag == NULL))
    {
        return -1;
    }
    if ((pt_len != 0u) && (pt == NULL))
    {
        return -1;
    }
    if ((aad_len != 0u) && (aad == NULL))
    {
        return -1;
    }
    st = CAAM_AES_EncryptTagGcm(CAAM, &s_handle, pt, ct, pt_len, iv, iv_len, aad, aad_len, key, 16u, tag, 16u);
    return (st == kStatus_Success) ? 0 : -1;
#else
    (void)key;
    (void)iv;
    (void)iv_len;
    (void)aad;
    (void)aad_len;
    (void)pt;
    (void)pt_len;
    (void)ct;
    (void)tag;
    return -1;
#endif
}

int caam_aes128_gcm_decrypt(const uint8_t key[16],
                            const uint8_t *iv, size_t iv_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *ct, size_t ct_len,
                            const uint8_t tag[16],
                            uint8_t *pt)
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    status_t st;
    if (!s_ready || (key == NULL) || (iv == NULL) || (iv_len != 12u) || (ct == NULL) || (tag == NULL) || (pt == NULL))
    {
        return -1;
    }
    if ((aad_len != 0u) && (aad == NULL))
    {
        return -1;
    }
    st = CAAM_AES_DecryptTagGcm(CAAM, &s_handle, ct, pt, ct_len, iv, iv_len, aad, aad_len, key, 16u, tag, 16u);
    return (st == kStatus_Success) ? 0 : -1;
#else
    (void)key;
    (void)iv;
    (void)iv_len;
    (void)aad;
    (void)aad_len;
    (void)ct;
    (void)ct_len;
    (void)tag;
    (void)pt;
    return -1;
#endif
}

void caam_hmac_sha256(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len,
                      uint8_t out[32])
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    size_t out_size = 32u;
    if (!s_ready || (out == NULL))
    {
        if (out != NULL)
        {
            memset(out, 0, 32);
        }
        return;
    }
    if (CAAM_HMAC(CAAM, &s_handle, kCAAM_HmacSha256, data, data_len, key, key_len, out, &out_size) != kStatus_Success)
    {
        memset(out, 0, 32);
    }
#else
    (void)key;
    (void)key_len;
    (void)data;
    (void)data_len;
    if (out != NULL)
    {
        memset(out, 0, 32);
    }
#endif
}
