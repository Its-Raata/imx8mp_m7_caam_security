/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * CAAM BLOB encapsulate / decapsulate on JR1.
 *
 * Descriptor matches MCUX CAAM_RedBlob_Encapsule / Decapsule (general
 * memory) and u-boot inline_cnstr_jobdesc_blob_encap (RED_KEY):
 *   KEY class-2 (16-byte modifier)
 *   SEQ IN / SEQ OUT
 *   OPERATION ENCAP or DECAP | BLOB
 */
#include "caam_blob.h"

#include <string.h>

#if defined(M7_CAAM_HW) && (M7_CAAM_HW)
#include "caam_imx8mp_jr.h"
#include "caam_imx8mp_regs.h"

static size_t blob_put_ptr(uint32_t *desc, size_t i, const void *p)
{
    desc[i++] = (uint32_t)(uintptr_t)p;
    return i;
}

static int blob_job(int encap,
                    const uint8_t modifier[16],
                    const uint8_t *in,
                    uint32_t in_len,
                    uint8_t *out,
                    uint32_t out_len)
{
    uint32_t *desc = caam8_dma_desc();
    uint8_t *scr   = caam8_dma_scratch();
    uint32_t st    = 0;
    uint8_t *modb;
    uint8_t *inb;
    uint8_t *outb;
    size_t i;

    if ((desc == NULL) || (scr == NULL) || (modifier == NULL) || (in == NULL) || (out == NULL))
    {
        return -1;
    }
    if ((in_len == 0u) || (out_len == 0u) || ((16u + in_len + out_len) > caam8_dma_scratch_size()))
    {
        return -1;
    }

    if (caam8_jr_recycle() != 0)
    {
        return -1;
    }

    modb = scr;
    inb  = scr + 16u;
    outb = inb + in_len;

    (void)memcpy(modb, modifier, 16u);
    (void)memcpy(inb, in, in_len);
    (void)memset(outb, 0xA5, out_len);

    i         = 1u;
    desc[i++] = CAAM8_CMD_KEY_C2 | 16u;
    i         = blob_put_ptr(desc, i, modb);
    desc[i++] = CAAM8_CMD_SEQ_IN | in_len;
    i         = blob_put_ptr(desc, i, inb);
    desc[i++] = CAAM8_CMD_SEQ_OUT | out_len;
    i         = blob_put_ptr(desc, i, outb);
    desc[i++] = encap ? CAAM8_CMD_OP_BLOB_ENCAP : CAAM8_CMD_OP_BLOB_DECAP;
    desc[0]   = CAAM8_CMD_JOB_HDR | (uint32_t)i;

    if (caam8_jr0_run(desc, &st) != 0)
    {
        return -2;
    }
    (void)memcpy(out, outb, out_len);
    return 0;
}
#endif

int caam_black_blob_wrap(const uint8_t modifier[16],
                         const uint8_t *key,
                         size_t key_len,
                         uint8_t *out,
                         uint32_t *out_len)
{
#if !(defined(M7_CAAM_HW) && (M7_CAAM_HW))
    (void)modifier;
    (void)key;
    (void)key_len;
    (void)out;
    (void)out_len;
    return -1;
#else
    uint32_t need;

    if ((key_len == 0u) || (key_len > 64u) || (out_len == NULL))
    {
        return -1;
    }
    need = (uint32_t)key_len + CAAM_BLOB_OVERHEAD;
    if (*out_len < need)
    {
        return -1;
    }
    if (blob_job(1, modifier, key, (uint32_t)key_len, out, need) != 0)
    {
        return -1;
    }
    *out_len = need;
    return 0;
#endif
}

int caam_black_blob_unwrap(const uint8_t modifier[16],
                           const uint8_t *blob,
                           uint32_t blob_len,
                           uint8_t *key,
                           size_t key_len)
{
#if !(defined(M7_CAAM_HW) && (M7_CAAM_HW))
    (void)modifier;
    (void)blob;
    (void)blob_len;
    (void)key;
    (void)key_len;
    return -1;
#else
    uint32_t need;

    if ((key_len == 0u) || (key_len > 64u))
    {
        return -1;
    }
    need = (uint32_t)key_len + CAAM_BLOB_OVERHEAD;
    if (blob_len != need)
    {
        return -1;
    }
    return blob_job(0, modifier, blob, need, key, (uint32_t)key_len);
#endif
}

int caam_blob_selftest(void)
{
#if !(defined(M7_CAAM_HW) && (M7_CAAM_HW))
    return -1;
#else
    static const uint8_t s_key[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    static const uint8_t s_mod[16] = {
        0x4d, 0x37, 0x43, 0x52, 0x42, 0x4c, 0x4f, 0x42, 0x4b, 0x41, 0x54, 0x30, 0x00, 0x00, 0x00, 0x01};
    uint8_t blob[16u + CAAM_BLOB_OVERHEAD];
    uint8_t plain[16];
    uint32_t blen = (uint32_t)sizeof(blob);

    if (caam_black_blob_wrap(s_mod, s_key, sizeof(s_key), blob, &blen) != 0)
    {
        return -1;
    }
    (void)memset(plain, 0, sizeof(plain));
    if (caam_black_blob_unwrap(s_mod, blob, blen, plain, sizeof(plain)) != 0)
    {
        return -1;
    }
    if (memcmp(plain, s_key, sizeof(s_key)) != 0)
    {
        return -1;
    }
    return 0;
#endif
}
