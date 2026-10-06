/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * Command handlers and key wrap for the M7 crypto RPMsg service.
 *
 * Engine: software (M7_USE_CAAM=0) or CAAM (default). Wrap: CAAM BLOB
 * when CAAM is on; Step-01 software wrap only when CAAM is compiled out.
 */
#include "crypto_service.h"
#include <string.h>

#if defined(M7_USE_CAAM) && (M7_USE_CAAM)
#include "caam_blob.h"
#include "caam_crypto.h"
#define ENG_AES_GCM_ENCRYPT caam_aes128_gcm_encrypt
#define ENG_AES_GCM_DECRYPT caam_aes128_gcm_decrypt
#define ENG_HMAC_SHA256     caam_hmac_sha256
#else
#include "sw_crypto.h"
#define ENG_AES_GCM_ENCRYPT sw_aes128_gcm_encrypt
#define ENG_AES_GCM_DECRYPT sw_aes128_gcm_decrypt
#define ENG_HMAC_SHA256     sw_hmac_sha256
#endif

#if !(defined(M7_USE_CAAM) && (M7_USE_CAAM))
/* Soft-blob wrap key — only when CAAM is compiled out. */
static const uint8_t s_wrap_key[16] = {
    0x4d, 0x37, 0x43, 0x52, 0x53, 0x4f, 0x46, 0x54, 0x42, 0x4c, 0x4f, 0x42, 0x4b, 0x45, 0x59, 0x31};
#endif

static uint8_t s_aes_key[M7CR_AES_KEY_LEN];
static uint8_t s_aes_valid;

static uint8_t s_hmac_key[M7CR_HMAC_KEY_MAX];
static uint16_t s_hmac_key_len;
static uint8_t s_hmac_valid;

/* Reported in PING payload (no M7 UART required on custom boards). */
static const char *s_engine_tag = "WAIT";

/* Counter mixed into the wrap IV / CAAM key modifier. */
static uint32_t s_iv_counter = 1u;

void crypto_service_init(void)
{
    memset(s_aes_key, 0, sizeof(s_aes_key));
    memset(s_hmac_key, 0, sizeof(s_hmac_key));
    s_aes_valid    = 0;
    s_hmac_valid   = 0;
    s_hmac_key_len = 0;
#if defined(M7_USE_CAAM) && (M7_USE_CAAM)
    s_engine_tag = "WAIT";
#else
    s_engine_tag = "OK-SW";
#endif
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t make_rsp(uint8_t *rsp,
                         uint32_t rsp_cap,
                         uint32_t req_id,
                         uint16_t status,
                         const uint8_t *payload,
                         uint32_t payload_len)
{
    m7cr_rsp_hdr_t *hdr;
    if (rsp_cap < sizeof(m7cr_rsp_hdr_t) + payload_len)
    {
        return 0;
    }
    hdr          = (m7cr_rsp_hdr_t *)rsp;
    hdr->magic   = M7CR_MAGIC;
    hdr->version = M7CR_VERSION;
    hdr->status  = status;
    hdr->req_id  = req_id;
    hdr->length  = payload_len;
    if ((payload_len != 0u) && (payload != NULL))
    {
        memcpy(rsp + sizeof(m7cr_rsp_hdr_t), payload, payload_len);
    }
    return (uint32_t)(sizeof(m7cr_rsp_hdr_t) + payload_len);
}

#if defined(M7_USE_CAAM) && (M7_USE_CAAM)
static int ensure_caam(void)
{
    if (caam_crypto_is_ready() != 0)
    {
        return 0;
    }
    s_engine_tag = caam_crypto_ping_step();
    return (caam_crypto_is_ready() != 0) ? 0 : -1;
}

static uint32_t blob_size_for_key(uint16_t key_len)
{
    return (uint32_t)(sizeof(m7cr_blob_hdr_t) + key_len + CAAM_BLOB_OVERHEAD);
}

static int wrap_key_blob(uint16_t type, const uint8_t *key, uint16_t key_len, uint8_t *out, uint32_t *out_len)
{
    m7cr_blob_hdr_t *hdr;
    uint32_t need = blob_size_for_key(key_len);
    uint32_t blen;

    if ((key_len == 0u) || (key_len > 64u) || (*out_len < need) || (ensure_caam() != 0))
    {
        return -1;
    }

    hdr = (m7cr_blob_hdr_t *)out;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic   = M7CR_BLOB_MAGIC;
    hdr->version = M7CR_BLOB_VERSION_CAAM;
    hdr->type    = type;
    hdr->key_len = key_len;
    memcpy(hdr->wrap_iv, "M7CRBLOB", 8);
    hdr->wrap_iv[12] = (uint8_t)(s_iv_counter >> 24);
    hdr->wrap_iv[13] = (uint8_t)(s_iv_counter >> 16);
    hdr->wrap_iv[14] = (uint8_t)(s_iv_counter >> 8);
    hdr->wrap_iv[15] = (uint8_t)(s_iv_counter);
    s_iv_counter++;

    blen = need - (uint32_t)sizeof(m7cr_blob_hdr_t);
    if (caam_black_blob_wrap(hdr->wrap_iv, key, key_len, out + sizeof(m7cr_blob_hdr_t), &blen) != 0)
    {
        return -1;
    }
    *out_len = (uint32_t)sizeof(m7cr_blob_hdr_t) + blen;
    return 0;
}

static int unwrap_key_blob(uint16_t expect_type, const uint8_t *blob, uint32_t blob_len, uint8_t *key, uint16_t *key_len)
{
    const m7cr_blob_hdr_t *hdr = (const m7cr_blob_hdr_t *)blob;
    uint32_t need;

    if ((blob_len < sizeof(m7cr_blob_hdr_t) + CAAM_BLOB_OVERHEAD) || (hdr->magic != M7CR_BLOB_MAGIC) ||
        (hdr->version != M7CR_BLOB_VERSION_CAAM) || (hdr->type != expect_type) || (hdr->key_len == 0u) ||
        (hdr->key_len > 64u) || (ensure_caam() != 0))
    {
        return -1;
    }
    need = (uint32_t)sizeof(m7cr_blob_hdr_t) + (uint32_t)hdr->key_len + CAAM_BLOB_OVERHEAD;
    if (blob_len < need)
    {
        return -1;
    }
    if (caam_black_blob_unwrap(hdr->wrap_iv, blob + sizeof(m7cr_blob_hdr_t),
                               (uint32_t)hdr->key_len + CAAM_BLOB_OVERHEAD, key, hdr->key_len) != 0)
    {
        return -1;
    }
    *key_len = hdr->key_len;
    return 0;
}
#else
static uint32_t blob_size_for_key(uint16_t key_len)
{
    uint16_t padded = (uint16_t)((key_len + 15u) & (uint16_t)~15u);
    return (uint32_t)(sizeof(m7cr_blob_hdr_t) + padded + M7CR_HMAC_LEN);
}

static int wrap_key_blob(uint16_t type, const uint8_t *key, uint16_t key_len, uint8_t *out, uint32_t *out_len)
{
    m7cr_blob_hdr_t *hdr;
    uint16_t padded = (uint16_t)((key_len + 15u) & (uint16_t)~15u);
    uint8_t plain[64];
    uint8_t *wrapped;
    uint8_t *mac;
    uint32_t need = blob_size_for_key(key_len);

    if ((key_len == 0u) || (key_len > 64u) || (padded > 64u) || (*out_len < need))
    {
        return -1;
    }

    hdr = (m7cr_blob_hdr_t *)out;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic   = M7CR_BLOB_MAGIC;
    hdr->version = M7CR_BLOB_VERSION_SOFT;
    hdr->type    = type;
    hdr->key_len = key_len;
    memset(hdr->wrap_iv, 0, 16);
    hdr->wrap_iv[12] = (uint8_t)(s_iv_counter >> 24);
    hdr->wrap_iv[13] = (uint8_t)(s_iv_counter >> 16);
    hdr->wrap_iv[14] = (uint8_t)(s_iv_counter >> 8);
    hdr->wrap_iv[15] = (uint8_t)(s_iv_counter);
    s_iv_counter++;

    memset(plain, 0, sizeof(plain));
    memcpy(plain, key, key_len);
    wrapped = out + sizeof(m7cr_blob_hdr_t);
    if (sw_aes128_cbc_encrypt(s_wrap_key, hdr->wrap_iv, plain, padded, wrapped) != 0)
    {
        return -1;
    }
    mac = wrapped + padded;
    sw_hmac_sha256(s_wrap_key, sizeof(s_wrap_key), wrapped, padded, mac);
    *out_len = need;
    return 0;
}

static int unwrap_key_blob(uint16_t expect_type, const uint8_t *blob, uint32_t blob_len, uint8_t *key, uint16_t *key_len)
{
    const m7cr_blob_hdr_t *hdr = (const m7cr_blob_hdr_t *)blob;
    uint16_t padded;
    const uint8_t *wrapped;
    const uint8_t *mac;
    uint8_t calc[M7CR_HMAC_LEN];
    uint8_t plain[64];
    uint8_t diff = 0;
    uint32_t i;

    if ((blob_len < sizeof(m7cr_blob_hdr_t) + M7CR_HMAC_LEN) || (hdr->magic != M7CR_BLOB_MAGIC) ||
        (hdr->version != M7CR_BLOB_VERSION_SOFT) || (hdr->type != expect_type) || (hdr->key_len == 0u) ||
        (hdr->key_len > 64u))
    {
        return -1;
    }
    padded = (uint16_t)((hdr->key_len + 15u) & (uint16_t)~15u);
    if (blob_len < sizeof(m7cr_blob_hdr_t) + padded + M7CR_HMAC_LEN)
    {
        return -1;
    }
    wrapped = blob + sizeof(m7cr_blob_hdr_t);
    mac     = wrapped + padded;
    sw_hmac_sha256(s_wrap_key, sizeof(s_wrap_key), wrapped, padded, calc);
    for (i = 0; i < M7CR_HMAC_LEN; i++)
    {
        diff |= (uint8_t)(calc[i] ^ mac[i]);
    }
    if (diff != 0u)
    {
        return -1;
    }
    if (sw_aes128_cbc_decrypt(s_wrap_key, hdr->wrap_iv, wrapped, padded, plain) != 0)
    {
        return -1;
    }
    memcpy(key, plain, hdr->key_len);
    *key_len = hdr->key_len;
    memset(plain, 0, sizeof(plain));
    return 0;
}
#endif

uint32_t crypto_service_handle(const uint8_t *req, uint32_t req_len, uint8_t *rsp, uint32_t rsp_cap)
{
    const m7cr_req_hdr_t *hdr;
    uint32_t req_id = 0;
    const uint8_t *payload;
    uint8_t outbuf[480];
    uint32_t out_len = 0;

    if ((req == NULL) || (rsp == NULL) || (req_len < sizeof(m7cr_req_hdr_t)))
    {
        return make_rsp(rsp, rsp_cap, 0, M7CR_ERR_LENGTH, NULL, 0);
    }

    hdr = (const m7cr_req_hdr_t *)req;
    req_id = hdr->req_id;
    if (hdr->magic != M7CR_MAGIC)
    {
        return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_MAGIC, NULL, 0);
    }
    if (hdr->version != M7CR_VERSION)
    {
        return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_VERSION, NULL, 0);
    }
    if (req_len < sizeof(m7cr_req_hdr_t) + hdr->length)
    {
        return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
    }
    payload = req + sizeof(m7cr_req_hdr_t);

    switch (hdr->cmd)
    {
        case M7CR_CMD_PING:
        {
            /* First ping brings JR1 up and runs ECB / GCM / HMAC / BLOB KATs. */
#if defined(M7_USE_CAAM) && (M7_USE_CAAM) && defined(M7_CAAM_TOUCH) && (M7_CAAM_TOUCH)
            s_engine_tag = caam_crypto_ping_step();
#elif defined(M7_USE_CAAM) && (M7_USE_CAAM)
            if (strcmp(s_engine_tag, "WAIT") == 0)
            {
                s_engine_tag = "NO-TOUCH";
            }
#endif
            {
                size_t n = strlen(s_engine_tag);
                if (n > sizeof(outbuf))
                {
                    n = sizeof(outbuf);
                }
                memcpy(outbuf, s_engine_tag, n);
                return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, outbuf, (uint32_t)n);
            }
        }

        case M7CR_CMD_STORE_AES_KEY:
            if (hdr->length != M7CR_AES_KEY_LEN)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            memcpy(s_aes_key, payload, M7CR_AES_KEY_LEN);
            s_aes_valid = 1;
            out_len     = sizeof(outbuf);
            if (wrap_key_blob(M7CR_BLOB_TYPE_AES128, s_aes_key, M7CR_AES_KEY_LEN, outbuf, &out_len) != 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_BLOB, NULL, 0);
            }
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, outbuf, out_len);

        case M7CR_CMD_LOAD_AES_BLOB:
        {
            uint16_t klen = 0;
            if (unwrap_key_blob(M7CR_BLOB_TYPE_AES128, payload, hdr->length, s_aes_key, &klen) != 0 ||
                klen != M7CR_AES_KEY_LEN)
            {
                s_aes_valid = 0;
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_BLOB, NULL, 0);
            }
            s_aes_valid = 1;
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, NULL, 0);
        }

        case M7CR_CMD_EXPORT_AES_BLOB:
            if (s_aes_valid == 0u)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_NO_KEY, NULL, 0);
            }
            out_len = sizeof(outbuf);
            if (wrap_key_blob(M7CR_BLOB_TYPE_AES128, s_aes_key, M7CR_AES_KEY_LEN, outbuf, &out_len) != 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_BLOB, NULL, 0);
            }
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, outbuf, out_len);

        case M7CR_CMD_ENCRYPT_GCM:
        {
            const uint8_t *iv;
            uint16_t aad_len, pt_len;
            const uint8_t *aad;
            const uint8_t *pt;
#if defined(M7_USE_CAAM) && (M7_USE_CAAM)
            if (caam_crypto_is_ready() == 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_CRYPTO, NULL, 0);
            }
#endif
            if (s_aes_valid == 0u)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_NO_KEY, NULL, 0);
            }
            if (hdr->length < M7CR_GCM_IV_LEN + 4u)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            iv      = payload;
            aad_len = rd_u16(payload + M7CR_GCM_IV_LEN);
            pt_len  = rd_u16(payload + M7CR_GCM_IV_LEN + 2u);
            if (hdr->length != (uint32_t)(M7CR_GCM_IV_LEN + 4u + aad_len + pt_len))
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            if ((uint32_t)pt_len + M7CR_GCM_TAG_LEN > sizeof(outbuf))
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            aad = payload + M7CR_GCM_IV_LEN + 4u;
            pt  = aad + aad_len;
            if (ENG_AES_GCM_ENCRYPT(s_aes_key, iv, M7CR_GCM_IV_LEN, aad, aad_len, pt, pt_len, outbuf,
                                      outbuf + pt_len) != 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_CRYPTO, NULL, 0);
            }
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, outbuf, (uint32_t)pt_len + M7CR_GCM_TAG_LEN);
        }

        case M7CR_CMD_DECRYPT_GCM:
        {
            const uint8_t *iv;
            uint16_t aad_len, ct_len;
            const uint8_t *aad;
            const uint8_t *ct;
            const uint8_t *tag;
#if defined(M7_USE_CAAM) && (M7_USE_CAAM)
            if (caam_crypto_is_ready() == 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_CRYPTO, NULL, 0);
            }
#endif
            if (s_aes_valid == 0u)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_NO_KEY, NULL, 0);
            }
            if (hdr->length < M7CR_GCM_IV_LEN + 4u + M7CR_GCM_TAG_LEN)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            iv      = payload;
            aad_len = rd_u16(payload + M7CR_GCM_IV_LEN);
            ct_len  = rd_u16(payload + M7CR_GCM_IV_LEN + 2u);
            if (hdr->length != (uint32_t)(M7CR_GCM_IV_LEN + 4u + aad_len + ct_len + M7CR_GCM_TAG_LEN))
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            if (ct_len > sizeof(outbuf))
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            aad = payload + M7CR_GCM_IV_LEN + 4u;
            ct  = aad + aad_len;
            tag = ct + ct_len;
            if (ENG_AES_GCM_DECRYPT(s_aes_key, iv, M7CR_GCM_IV_LEN, aad, aad_len, ct, ct_len, tag, outbuf) != 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_AUTH, NULL, 0);
            }
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, outbuf, ct_len);
        }

        case M7CR_CMD_STORE_HMAC_KEY:
        {
            uint16_t klen;
            if (hdr->length < 2u)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            klen = rd_u16(payload);
            if ((klen == 0u) || (klen > M7CR_HMAC_KEY_MAX) || (hdr->length != (uint32_t)(2u + klen)))
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_LENGTH, NULL, 0);
            }
            memcpy(s_hmac_key, payload + 2, klen);
            s_hmac_key_len = klen;
            s_hmac_valid   = 1;
            out_len        = sizeof(outbuf);
            if (wrap_key_blob(M7CR_BLOB_TYPE_HMAC, s_hmac_key, klen, outbuf, &out_len) != 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_BLOB, NULL, 0);
            }
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, outbuf, out_len);
        }

        case M7CR_CMD_LOAD_HMAC_BLOB:
        {
            uint16_t klen = 0;
            if (unwrap_key_blob(M7CR_BLOB_TYPE_HMAC, payload, hdr->length, s_hmac_key, &klen) != 0)
            {
                s_hmac_valid = 0;
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_BLOB, NULL, 0);
            }
            s_hmac_key_len = klen;
            s_hmac_valid   = 1;
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, NULL, 0);
        }

        case M7CR_CMD_EXPORT_HMAC_BLOB:
            if (s_hmac_valid == 0u)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_NO_KEY, NULL, 0);
            }
            out_len = sizeof(outbuf);
            if (wrap_key_blob(M7CR_BLOB_TYPE_HMAC, s_hmac_key, s_hmac_key_len, outbuf, &out_len) != 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_BLOB, NULL, 0);
            }
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, outbuf, out_len);

        case M7CR_CMD_SIGN_HMAC:
#if defined(M7_USE_CAAM) && (M7_USE_CAAM)
            if (caam_crypto_is_ready() == 0)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_CRYPTO, NULL, 0);
            }
#endif
            if (s_hmac_valid == 0u)
            {
                return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_NO_KEY, NULL, 0);
            }
            ENG_HMAC_SHA256(s_hmac_key, s_hmac_key_len, payload, hdr->length, outbuf);
            return make_rsp(rsp, rsp_cap, req_id, M7CR_OK, outbuf, M7CR_HMAC_LEN);

        default:
            return make_rsp(rsp, rsp_cap, req_id, M7CR_ERR_CMD, NULL, 0);
    }
}
