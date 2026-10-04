/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM crypto backend for i.MX8MP (M7).
 *
 * M7_CAAM_HW=0  — scaffold returns errors (build/wiring check).
 * M7_CAAM_HW=1  — JR1 driver + AES-GCM / HMAC-SHA256 (KATs on first ping).
 */
#include "caam_crypto.h"

#include <stdio.h>
#include <string.h>

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
#include "caam_imx8mp_jr.h"
#include "caam_imx8mp_regs.h"
#endif

static uint8_t s_ready;
static char s_fail_buf[40];
static const char *s_fail_tag = "FAIL";

const char *caam_crypto_fail_tag(void)
{
    return s_fail_tag;
}

static void set_fail(const char *tag)
{
    s_fail_tag = tag;
}

const char *caam_crypto_probe_tag(void)
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    if (caam8_probe_readonly() == 0)
    {
        (void)sprintf(s_fail_buf, "SEE:%s", caam8_probe_detail());
    }
    else
    {
        (void)sprintf(s_fail_buf, "FAIL-P:%s", caam8_probe_detail());
    }
    return s_fail_buf;
#else
    return "FAIL-NOHW";
#endif
}

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
static int caam8_selftest_ecb(void);
static int caam8_selftest_gcm(void);
static int caam8_selftest_hmac(void);
#endif

int caam_crypto_is_ready(void)
{
    return (s_ready != 0u) ? 1 : 0;
}

const char *caam_crypto_ping_step(void)
{
#if !(defined(M7_CAAM_HW) && (M7_CAAM_HW))
    return "FAIL-NOHW";
#else
    if (s_ready != 0u)
    {
        return "OK-CAAM";
    }

    /* One ping: clocks → probe → rings → ECB / GCM / HMAC KATs. */
    caam8_clocks_on();
    s_fail_tag = caam_crypto_probe_tag();
    if (strncmp(s_fail_tag, "SEE:", 4) != 0)
    {
        return s_fail_tag;
    }

    if (caam8_jr_setup_rings() != 0)
    {
        set_fail("FAIL-IRSA");
        return s_fail_tag;
    }
    if (caam8_selftest_ecb() != 0)
    {
        set_fail("FAIL-KAT");
        return s_fail_tag;
    }
    if (caam8_jr_recycle() != 0)
    {
        set_fail("FAIL-IRSA");
        return s_fail_tag;
    }
    if (caam8_selftest_gcm() != 0)
    {
        set_fail("FAIL-GCM");
        return s_fail_tag;
    }
    if (caam8_jr_recycle() != 0)
    {
        set_fail("FAIL-IRSA");
        return s_fail_tag;
    }
    if (caam8_selftest_hmac() != 0)
    {
        set_fail("FAIL-HMAC");
        return s_fail_tag;
    }

    s_ready = 1;
    return "OK-CAAM";
#endif
}

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)

/* NIST AES-128-ECB one-block KAT */
static const uint8_t s_kat_key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
static const uint8_t s_kat_pt[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
static const uint8_t s_kat_ct[16] = {
    0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60, 0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97};

/* NIST AES-GCM Test Case 2 (empty AAD, 16-byte PT, 12-byte IV). */
static const uint8_t s_gcm_key[16] = {0};
static const uint8_t s_gcm_iv[12]  = {0};
static const uint8_t s_gcm_pt[16]  = {0};
static const uint8_t s_gcm_ct[16]  = {
    0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92, 0xf3, 0x28, 0xc2, 0xb9, 0x71, 0xb2, 0xfe, 0x78};
static const uint8_t s_gcm_tag[16] = {
    0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd, 0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf};

/* RFC 4231 HMAC-SHA256 Test Case 1. */
static const uint8_t s_hmac_key[20] = {
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
static const uint8_t s_hmac_msg[8] = {0x48, 0x69, 0x20, 0x54, 0x68, 0x65, 0x72, 0x65};
static const uint8_t s_hmac_mac[32] = {
    0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
    0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};

/* MCUXpresso SDK templateAesGcm (32-bit pointers). */
static const uint32_t s_gcm_tmpl[] = {
    CAAM8_CMD_JOB_HDR,
    CAAM8_CMD_KEY_C1,
    0u,
    CAAM8_CMD_OP_AES_GCM_FIN,
    CAAM8_CMD_LOAD_C1_ICVSZ_IMM,
    0u,
    CAAM8_CMD_FIFOLD_C1_IV_FLUSH,
    0u,
    CAAM8_CMD_FIFOLD_C1_AAD_FLUSH,
    0u,
    CAAM8_CMD_FIFOLD_C1_MSG_EXT,
    0u,
    0u,
    CAAM8_CMD_FIFOST_MSG_EXT,
    0u,
    0u,
    CAAM8_CMD_JMP_C1DONE,
    CAAM8_CMD_LOAD_IMM_CLRW,
    CAAM8_CMD_CLRW_C1D_C1DS,
    CAAM8_CMD_LOAD_IMM_C1DS,
    0u,
    CAAM8_CMD_LOAD_C1_ICVSZ_IMM,
    0u,
    CAAM8_CMD_OP_AES_GCM_ICV,
    CAAM8_CMD_FIFOLD_C1_ICV,
    0u,
};

static size_t caam8_put_ptr(uint32_t *desc, size_t i, const void *p)
{
    desc[i++] = (uint32_t)(uintptr_t)p;
    return i;
}

static int caam8_aes_ecb_encrypt(const uint8_t *key, const uint8_t *pt, uint8_t *ct, size_t len)
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
    (void)memset(c, 0xA5, len);

    i         = 1u;
    desc[i++] = CAAM8_CMD_KEY_C1 | 16u;
    i         = caam8_put_ptr(desc, i, k);
    desc[i++] = CAAM8_CMD_OP_AES_ECB_ENC;
    desc[i++] = CAAM8_CMD_FIFOLD_C1_MSG_LAST1 | (uint32_t)len;
    i         = caam8_put_ptr(desc, i, p);
    desc[i++] = CAAM8_CMD_FIFOST_MSG | (uint32_t)len;
    i         = caam8_put_ptr(desc, i, c);
    desc[0]   = CAAM8_CMD_JOB_HDR | i;

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

    iv_last  = ((aad_len == 0u) && (in_len == 0u)) ? 1 : 0;
    aad_last = (in_len == 0u) ? 1 : 0;

    (void)memcpy(desc, s_gcm_tmpl, sizeof(s_gcm_tmpl));
    desc[0] |= (uint32_t)(sizeof(s_gcm_tmpl) / sizeof(s_gcm_tmpl[0]));
    desc[1] |= 16u;
    desc[2]  = (uint32_t)(uintptr_t)k;
    if (encrypt)
    {
        desc[3] |= 1u;
    }
    desc[5]  = 16u;
    desc[6] |= (uint32_t)iv_len | ((iv_last != 0) ? CAAM8_FIFOLD_LC1 : 0u);
    desc[7]  = (uint32_t)(uintptr_t)ivb;
    desc[8] |= (uint32_t)aad_len | (((iv_last == 0) && (aad_last != 0)) ? CAAM8_FIFOLD_LC1 : 0u);
    desc[9]  = (uint32_t)(uintptr_t)((aad_len != 0u) ? aadb : ivb);
    desc[11] = (uint32_t)(uintptr_t)((in_len != 0u) ? inb : ivb);
    desc[12] = (uint32_t)in_len;
    desc[14] = (uint32_t)(uintptr_t)((in_len != 0u) ? outb : ivb);
    desc[15] = (uint32_t)in_len;

    if (encrypt)
    {
        desc[16] = CAAM8_CMD_STORE_C1_CTX | 16u;
        desc[17] = (uint32_t)(uintptr_t)tagb;
        desc[18] = CAAM8_CMD_HALT;
        desc[0]  = CAAM8_CMD_JOB_HDR | 19u;
    }
    else
    {
        desc[22]  = 16u;
        desc[24] |= 16u;
        desc[25]  = (uint32_t)(uintptr_t)tagb;
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

static int caam8_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32])
{
    uint32_t *desc = caam8_dma_desc();
    uint8_t *scr   = caam8_dma_scratch();
    uint32_t st    = 0;
    uint8_t *k;
    uint8_t *m;
    uint8_t *o;
    size_t i;

    if ((desc == NULL) || (scr == NULL) || (key == NULL) || (out == NULL) || (key_len == 0u) || (key_len > 64u) ||
        ((data_len != 0u) && (data == NULL)) || ((16u + key_len + data_len + 32u) > caam8_dma_scratch_size()))
    {
        return -1;
    }

    k = scr;
    m = scr + 64u;
    o = scr + 64u + ((data_len + 15u) & ~(size_t)15u);
    (void)memcpy(k, key, key_len);
    if (data_len != 0u)
    {
        (void)memcpy(m, data, data_len);
    }
    (void)memset(o, 0xA5, 32u);

    i         = 1u;
    desc[i++] = CAAM8_CMD_KEY_C2 | CAAM8_CMD_KEY_NWB | (uint32_t)key_len;
    i         = caam8_put_ptr(desc, i, k);
    desc[i++] = CAAM8_CMD_OP_HMAC_SHA256;
    desc[i++] = CAAM8_CMD_FIFOLD_C2_MSG_LAST2 | (uint32_t)data_len;
    i         = caam8_put_ptr(desc, i, (data_len != 0u) ? m : k);
    desc[i++] = CAAM8_CMD_STORE_C2_CTX | 32u;
    i         = caam8_put_ptr(desc, i, o);
    desc[0]   = CAAM8_CMD_JOB_HDR | i;

    if (caam8_jr0_run(desc, &st) != 0)
    {
        return -2;
    }
    (void)memcpy(out, o, 32u);
    return 0;
}

static int caam8_selftest_ecb(void)
{
    uint8_t out[16];

    (void)memset(out, 0, sizeof(out));
    if (caam8_aes_ecb_encrypt(s_kat_key, s_kat_pt, out, 16u) != 0)
    {
        return -1;
    }
    return (memcmp(out, s_kat_ct, 16u) == 0) ? 0 : -2;
}

static int caam8_selftest_gcm(void)
{
    uint8_t ct[16];
    uint8_t tag[16];
    uint8_t pt[16];

    (void)memset(ct, 0, sizeof(ct));
    (void)memset(tag, 0, sizeof(tag));
    (void)memset(pt, 0, sizeof(pt));
    if (caam8_aes_gcm(1, s_gcm_key, s_gcm_iv, 12u, NULL, 0u, s_gcm_pt, 16u, ct, tag) != 0)
    {
        return -1;
    }
    if ((memcmp(ct, s_gcm_ct, 16u) != 0) || (memcmp(tag, s_gcm_tag, 16u) != 0))
    {
        return -2;
    }
    if (caam8_jr_recycle() != 0)
    {
        return -3;
    }
    if (caam8_aes_gcm(0, s_gcm_key, s_gcm_iv, 12u, NULL, 0u, ct, 16u, pt, tag) != 0)
    {
        return -4;
    }
    return (memcmp(pt, s_gcm_pt, 16u) == 0) ? 0 : -5;
}

static int caam8_selftest_hmac(void)
{
    uint8_t mac[32];

    (void)memset(mac, 0, sizeof(mac));
    if (caam8_hmac_sha256(s_hmac_key, sizeof(s_hmac_key), s_hmac_msg, sizeof(s_hmac_msg), mac) != 0)
    {
        return -1;
    }
    return (memcmp(mac, s_hmac_mac, 32u) == 0) ? 0 : -2;
}

#endif /* M7_CAAM_HW */

int caam_crypto_init(void)
{
    /* Bring-up is deferred to the first PING (keeps RPMsg up if CAAM is dead). */
    s_ready    = 0;
    s_fail_tag = "FAIL";
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    return 0;
#else
    set_fail("FAIL-NOHW");
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

void caam_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32])
{
#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
    if ((out != NULL) && (s_ready != 0u) && (caam8_hmac_sha256(key, key_len, data, data_len, out) == 0))
    {
        return;
    }
#else
    (void)key;
    (void)key_len;
    (void)data;
    (void)data_len;
#endif
    if (out != NULL)
    {
        memset(out, 0, 32);
    }
}
