/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * i.MX8MP CAAM register map (crypto@30900000, JR step 0x1000).
 * Not the RT117x CAAM_Type (JR step 0x10000).
 */
#ifndef CAAM_IMX8MP_REGS_H_
#define CAAM_IMX8MP_REGS_H_

#include <stdint.h>

#define CAAM8_BASE          (0x30900000u)
#define CAAM8_JR0_BASE      (0x30901000u)
#define CAAM8_JR1_BASE      (0x30902000u)
#define CAAM8_JR2_BASE      (0x30903000u)

/*
 * Use JR1 on i.MX8MP: JR0 is HAB-reserved (NS reads 0).
 * CAAM JRaDID PRIM_DID is the AIPSTZ/interconnect master ID, not RDC DID:
 *   A53 = 1, SDMA = 3, M7 = 6 (NXP imx8mp-aipstz.h). 0 is EDMA, not M7.
 * Stock firmware leaves JR1 at MID 1, so M7 MMIO of this page is ignored (0).
 * ATF must lock JR1MID = LDID|6 and start JR1 (see linux/atf/).
 */
#define CAAM8_JR_BASE       CAAM8_JR1_BASE

/* Controller (page 0 — mostly invisible from M7) */
#define CAAM8_MCFGR_OFF     (0x0004u)
#define CAAM8_JR1DID_OFF    (0x0018u)
#define CAAM8_JRSTART_OFF   (0x005Cu)

#define CAAM8_JRSTART_JR1      (0x00000002u)

/*
 * Job ring (relative to JRn base) — u-boot struct jr_regs.
 * On IMX8M the ring base pair is {_h at +0, _l at +4}.
 */
#define CAAM8_JR_IRBAR_HI   (0x0000u)
#define CAAM8_JR_IRBAR_LO   (0x0004u)
#define CAAM8_JR_IRSR       (0x000Cu)
#define CAAM8_JR_IRSA       (0x0014u)
#define CAAM8_JR_IRJA       (0x001Cu)
#define CAAM8_JR_ORBAR_HI   (0x0020u)
#define CAAM8_JR_ORBAR_LO   (0x0024u)
#define CAAM8_JR_ORSR       (0x002Cu)
#define CAAM8_JR_ORJR       (0x0034u)
#define CAAM8_JR_ORSF       (0x003Cu)
#define CAAM8_JR_JRINT      (0x004Cu)
#define CAAM8_JR_JRCFG_LS   (0x0054u)
#define CAAM8_JR_JRCR       (0x006Cu)

#define CAAM8_JRCFG_IMSK    (0x01u)

/*
 * DMA arena inside M7 reserved DDR (imx8mp-evk-rpmsg.dts m4@80000000).
 * TCM is not CAAM-visible.
 */
#define CAAM8_DMA_ARENA     (0x80080000u)

/*
 * Descriptor command words from u-boot drivers/crypto/fsl/desc.h and the
 * MCUXpresso SDK templateAesGcm / HMAC path. Length rides in the command
 * word; the data pointer is the word that follows. EXT FIFO words put the
 * 32-bit length after the pointer.
 */
#define CAAM8_CMD_JOB_HDR               (0xB0800000u)
#define CAAM8_CMD_KEY_C1                (0x02000000u)
#define CAAM8_CMD_KEY_C2                (0x04000000u)
#define CAAM8_CMD_KEY_NWB               (0x00004000u)
#define CAAM8_CMD_OP_AES_ECB_ENC        (0x8210020Du)
#define CAAM8_CMD_OP_AES_GCM_FIN        (0x82100908u)
#define CAAM8_CMD_OP_AES_GCM_ICV        (0x82100902u)
#define CAAM8_CMD_OP_HMAC_SHA256        (0x8443001Du)
#define CAAM8_CMD_LOAD_C1_ICVSZ_IMM     (0x12830004u)
#define CAAM8_CMD_FIFOLD_C1_IV_FLUSH    (0x22210000u)
#define CAAM8_CMD_FIFOLD_C1_AAD_FLUSH   (0x22310000u)
#define CAAM8_CMD_FIFOLD_C1_MSG_EXT     (0x22530000u)
#define CAAM8_CMD_FIFOLD_C1_ICV         (0x223B0000u)
#define CAAM8_CMD_FIFOLD_C1_MSG_LAST1   (0x22120000u)
#define CAAM8_CMD_FIFOLD_C2_MSG_LAST2   (0x24140000u)
#define CAAM8_CMD_FIFOST_MSG            (0x60300000u)
#define CAAM8_CMD_FIFOST_MSG_EXT        (0x60700000u)
#define CAAM8_CMD_STORE_C1_CTX          (0x52200000u)
#define CAAM8_CMD_STORE_C2_CTX          (0x54200000u)
#define CAAM8_CMD_HALT                  (0xA0C00000u) /* MCUX DESC_HALT */
#define CAAM8_CMD_JMP_C1DONE            (0xA3001201u)
#define CAAM8_FIFOLD_LC1                (0x00020000u) /* LAST1, not SGF */
#define CAAM8_CMD_LOAD_IMM_CLRW         (0x10880004u)
#define CAAM8_CMD_CLRW_C1D_C1DS         (0x08000004u)
#define CAAM8_CMD_LOAD_IMM_C1DS         (0x12820004u)
#define CAAM8_CMD_SEQ_IN                (0xF0000000u)
#define CAAM8_CMD_SEQ_OUT               (0xF8000000u)
#define CAAM8_CMD_OP_BLOB_ENCAP         (0x870D0000u) /* ENCAP | BLOB */
#define CAAM8_CMD_OP_BLOB_DECAP         (0x860D0000u) /* DECAP | BLOB */

static inline uint32_t caam8_rd(uint32_t base, uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(base + off);
}

static inline void caam8_wr(uint32_t base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(uintptr_t)(base + off) = val;
}

#endif /* CAAM_IMX8MP_REGS_H_ */
