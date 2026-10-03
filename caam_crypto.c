/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM crypto backend for i.MX8MP (M7).
 *
 * M7_CAAM_HW=0  — scaffold returns errors (build/wiring check).
 * M7_CAAM_HW=1  — JR0 driver + AES-GCM (ECB KAT in init).
 */
#include "caam_crypto.h"

#include <string.h>

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
#include "caam_imx8mp_jr.h"
#endif

static uint8_t s_ready;

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)

/* MCUX-style single-job descriptors (32-bit pointer mode). */
static const uint32_t s_tmpl_aes_ecb[] = {
    0xB0800000u, /* HEADER */
    0x02000000u, /* KEY */
    0x00000000u, /* key addr */
    0x22530000u, /* FIFO LOAD MSG EXT */
    0x00000000u, /* src */
    0x00000000u, /* src size */
    0x60700000u, /* FIFO STORE MSG EXT */
    0x00000000u, /* dst */
    0x00000000u, /* dst size */
    0x82100200u, /* OPERATION AES ECB (OR ENC for encrypt) */
};

static const uint32_t s_tmpl_aes_gcm[] = {
    0xB0800000u, /* HEADER */
    0x02000000u, /* KEY */
    0x00000000u, /* key addr */
    0x82100908u, /* OPERATION AES GCM Decrypt Finalize (OR ENC) */
    0x12830004u, /* LOAD C1 ICV Size IMM */
    0x00000000u, /* ICV size */
    0x22210000u, /* FIFO LOAD IV flush */
    0x00000000u, /* IV addr */
    0x22310000u, /* FIFO LOAD AAD flush */
    0x00000000u, /* AAD addr */
    0x22530000u, /* FIFO LOAD message EXT */
    0x00000000u, /* msg addr */
    0x00000000u, /* msg size */
    0x60700000u, /* FIFO STORE message EXT */
    0x00000000u, /* dst addr */
    0x00000000u, /* dst size */
    0xA3001201u, /* JMP checkpoint */
    0x10880004u, /* LOAD Clear Written IMM */
    0x08000004u,
    0x12820004u, /* LOAD C1DS IMM */
    0x00000000u,
    0x12830004u, /* LOAD C1 ICV Size IMM */
    0x00000000u,
    0x82100902u, /* OPERATION AES GCM Update ICV_TEST */
    0x223B0000u, /* FIFO LOAD ICV */
    0x00000000u, /* ICV addr */
};

#define DESC_HALT (0xA0000000u)

/* NIST AES-128-ECB one-block KAT */
static const uint8_t s_kat_key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
static const uint8_t s_kat_pt[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
static const uint8_t s_kat_ct[16] = {
    0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60, 0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97};

static int caam8_aes_ecb_encrypt(const uint8_t *key,
                                 const uint8_t *pt,
                                 uint8_t *ct,
                                 size_t len)
{
    uint32_t *desc = caam8_dma_desc();
    uint8_t *scr   = caam8_dma_scratch();
    uint32_t st    = 0;
    uint8_t *k;
    uint8_t *p;
    uint8_t *c;
    size_t i;

    if ((desc == NULL) || (scr == NULL) || (key == NULL) || (pt == NULL) || (ct == NULL) || (len == 0u) ||
        ((len % 16u) != 0u) || (len > 256u))
    {
        return -1;
    }

    k = scr;
    p = scr + 32u;
    c = scr + 32u + 256u;
    (void)memcpy(k, key, 16u);
    (void)memcpy(p, pt, len);

    for (i = 0; i < (sizeof(s_tmpl_aes_ecb) / sizeof(s_tmpl_aes_ecb[0])); i++)
    {
        desc[i] = s_tmpl_aes_ecb[i];
    }
    desc[0] |= (uint32_t)(sizeof(s_tmpl_aes_ecb) / sizeof(s_tmpl_aes_ecb[0]));
    desc[1] |= 16u;
    desc[2] = (uint32_t)(uintptr_t)k;
    desc[4] = (uint32_t)(uintptr_t)p;
    desc[5] = (uint32_t)len;
    desc[7] = (uint32_t)(uintptr_t)c;
    desc[8] = (uint32_t)len;
    desc[9] |= 1u; /* ENC */

    if (caam8_jr0_run(desc, &st) != 0)
    {
        return -2;
    }
    (void)memcpy(ct, c, len);
    return 0;
}

static int caam8_aes_gcm(int encrypt,
                         const uint8_t key[16],
                         const uint8_t *iv,
                         size_t iv_len,
                         const uint8_t *aad,
                         size_t aad_len,
                         const uint8_t *in,
                         size_t in_len,
                         uint8_t *out,
                         uint8_t tag[16])
{
    uint32_t *desc = caam8_dma_desc();
    uint8_t *scr   = caam8_dma_scratch();
    uint32_t st    = 0;
    uint8_t *k;
    uint8_t *ivb;
    uint8_t *aadb;
    uint8_t *inb;
    uint8_t *outb;
    uint8_t *tagb;
    size_t off;
    size_t i;
    size_t nwords;
    int iv_last;
    int aad_last;

    if ((desc == NULL) || (scr == NULL) || (key == NULL) || (iv == NULL) || (iv_len != 12u) || (tag == NULL))
    {
        return -1;
    }
    if ((in_len != 0u) && ((in == NULL) || (out == NULL)))
    {
        return -1;
    }
    if ((aad_len != 0u) && (aad == NULL))
    {
        return -1;
    }
    if ((16u + 16u + aad_len + in_len + in_len + 16u) > caam8_dma_scratch_size())
    {
        return -1;
    }

    off  = 0;
    k    = scr + off;
    off += 16u;
    ivb  = scr + off;
    off += 16u;
    aadb = scr + off;
    off += aad_len;
    inb  = scr + off;
    off += in_len;
    outb = scr + off;
    off += in_len;
    tagb = scr + off;

    (void)memcpy(k, key, 16u);
    (void)memcpy(ivb, iv, iv_len);
    if (aad_len != 0u)
    {
        (void)memcpy(aadb, aad, aad_len);
    }
    if (in_len != 0u)
    {
        (void)memcpy(inb, in, in_len);
    }
    if (!encrypt)
    {
        (void)memcpy(tagb, tag, 16u);
    }

    nwords = sizeof(s_tmpl_aes_gcm) / sizeof(s_tmpl_aes_gcm[0]);
    for (i = 0; i < nwords; i++)
    {
        desc[i] = s_tmpl_aes_gcm[i];
    }
    desc[0] |= (uint32_t)nwords;
    desc[1] |= 16u;
    desc[2] = (uint32_t)(uintptr_t)k;
    if (encrypt)
    {
        desc[3] |= 1u;
    }
    desc[5] = 16u;

    iv_last  = ((aad_len == 0u) && (in_len == 0u)) ? 1 : 0;
    aad_last = (in_len == 0u) ? 1 : 0;
    desc[6] |= (uint32_t)iv_len;
    desc[7] = (uint32_t)(uintptr_t)ivb;
    if (iv_last != 0)
    {
        desc[6] |= 0x01000000u; /* LC1 */
    }
    desc[8] |= (uint32_t)aad_len;
    desc[9] = (uint32_t)(uintptr_t)((aad_len != 0u) ? aadb : ivb);
    if ((iv_last == 0) && (aad_last != 0))
    {
        desc[8] |= 0x01000000u;
    }
    desc[11] = (uint32_t)(uintptr_t)((in_len != 0u) ? inb : ivb);
    desc[12] = (uint32_t)in_len;
    desc[14] = (uint32_t)(uintptr_t)((in_len != 0u) ? outb : ivb);
    desc[15] = (uint32_t)in_len;

    if (encrypt)
    {
        desc[16] = 0x52200000u | 16u; /* STORE C1CTX → tag */
        desc[17] = (uint32_t)(uintptr_t)tagb;
        desc[18] = DESC_HALT;
    }
    else
    {
        desc[22] = 16u;
        desc[24] |= 16u;
        desc[25] = (uint32_t)(uintptr_t)tagb;
    }

    if (caam8_jr0_run(desc, &st) != 0)
    {
        return -2;
    }
    if (in_len != 0u)
    {
        (void)memcpy(out, outb, in_len);
    }
    if (encrypt)
    {
        (void)memcpy(tag, tagb, 16u);
    }
    return 0;
}

static int caam8_selftest_ecb(void)
{
    uint8_t out[16];

    if (caam8_aes_ecb_encrypt(s_kat_key, s_kat_pt, out, 16u) != 0)
    {
        return -1;
    }
    if (memcmp(out, s_kat_ct, 16u) != 0)
    {
        return -2;
    }
    return 0;
}

#endif /* M7_CAAM_HW */

int caam_crypto_init(void)
{
    s_ready = 0;

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    if (caam8_jr0_init() != 0)
    {
        return -1;
    }
    if (caam8_selftest_ecb() != 0)
    {
        return -2;
    }
    s_ready = 1;
    return 0;
#else
    return -1;
#endif
}

int caam_aes128_gcm_encrypt(const uint8_t key[16],
                            const uint8_t *iv,
                            size_t iv_len,
                            const uint8_t *aad,
                            size_t aad_len,
                            const uint8_t *pt,
                            size_t pt_len,
                            uint8_t *ct,
                            uint8_t tag[16])
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    if (!s_ready)
    {
        return -1;
    }
    return caam8_aes_gcm(1, key, iv, iv_len, aad, aad_len, pt, pt_len, ct, tag);
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
                            const uint8_t *iv,
                            size_t iv_len,
                            const uint8_t *aad,
                            size_t aad_len,
                            const uint8_t *ct,
                            size_t ct_len,
                            const uint8_t tag[16],
                            uint8_t *pt)
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    if (!s_ready)
    {
        return -1;
    }
    {
        uint8_t tag_copy[16];
        (void)memcpy(tag_copy, tag, 16u);
        return caam8_aes_gcm(0, key, iv, iv_len, aad, aad_len, ct, ct_len, pt, tag_copy);
    }
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

void caam_hmac_sha256(const uint8_t *key,
                      size_t key_len,
                      const uint8_t *data,
                      size_t data_len,
                      uint8_t out[32])
{
    /* HMAC-on-CAAM is next; keep soft path via M7_USE_CAAM=0 until then. */
    (void)key;
    (void)key_len;
    (void)data;
    (void)data_len;
    if (out != NULL)
    {
        memset(out, 0, 32);
    }
}
