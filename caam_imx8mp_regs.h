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

/* Controller */
#define CAAM8_MCFGR_OFF     (0x0004u)
#define CAAM8_JRSTART_OFF   (0x005Cu)

#define CAAM8_MCFGR_LONG_PTR   (0x00010000u)
#define CAAM8_JRSTART_JR0      (0x00000001u)

/* Job ring (relative to JRn base) — Linux struct caam_job_ring */
#define CAAM8_JR_IRBAR_LO   (0x0000u)
#define CAAM8_JR_IRBAR_HI   (0x0004u)
#define CAAM8_JR_IRSR       (0x000Cu)
#define CAAM8_JR_IRSA       (0x0014u)
#define CAAM8_JR_IRJA       (0x001Cu)
#define CAAM8_JR_ORBAR_LO   (0x0020u)
#define CAAM8_JR_ORBAR_HI   (0x0024u)
#define CAAM8_JR_ORSR       (0x002Cu)
#define CAAM8_JR_ORJR       (0x0034u)
#define CAAM8_JR_ORSF       (0x003Cu)
#define CAAM8_JR_JRSTA      (0x0044u)
#define CAAM8_JR_JRINT      (0x004Cu)
#define CAAM8_JR_JRCFG_MS   (0x0050u)
#define CAAM8_JR_JRCFG_LS   (0x0054u)
#define CAAM8_JR_JRCR       (0x006Cu)

#define CAAM8_JRCFG_IMSK    (0x01u)
#define CAAM8_JRCR_RESET    (0x01u)

/*
 * DMA arena inside M7 reserved DDR (imx8mp-evk-rpmsg.dts m4@80000000).
 * TCM is not CAAM-visible; keep descriptors and I/O buffers here.
 */
#define CAAM8_DMA_ARENA     (0x80080000u)

static inline uint32_t caam8_rd(uint32_t base, uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(base + off);
}

static inline void caam8_wr(uint32_t base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(uintptr_t)(base + off) = val;
}

#endif /* CAAM_IMX8MP_REGS_H_ */
