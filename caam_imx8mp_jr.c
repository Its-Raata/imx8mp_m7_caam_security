/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * i.MX8MP CAAM JR1: program rings in reserved DDR, poll-mode run.
 * (JR0 is HAB-reserved on this SoC; NS sees zeros there.)
 *
 * ATF must own page 0: lock JR1MID = LDID|6 and start JR1 (JRSTARTR bit 1).
 * The M7 cannot start the ring — page 0 reads back as zero from DID1.
 */
#include "caam_imx8mp_jr.h"
#include "caam_imx8mp_regs.h"

#include "board.h"
#include "fsl_clock.h"
#include "fsl_common.h"

#include <stdio.h>
#include <string.h>

/*
 * Depth 1: MCFGR (and the PS pointer-size bit) reads back 0 from the M7, so
 * the ring stride is unknowable. An 8-byte input slot and a 16-byte output
 * entry are valid under either PS; with one slot, wrap never disagrees.
 */
#define CAAM8_JR_DEPTH     (1u)
#define CAAM8_DESC_WORDS   (64u)
#define CAAM8_SCRATCH_SIZE (1024u)
/* Keep short: long MMIO waits block RPMsg (ping timeout). */
#define CAAM8_JOB_TIMEOUT  (400000u)

typedef struct
{
    uint32_t inpring[2];  /* desc ptr, high half (0) */
    uint32_t outring[4];  /* PS=0: desc,status  PS=1: desc_lo,desc_hi,status */
    uint32_t desc[CAAM8_DESC_WORDS];
    uint8_t scratch[CAAM8_SCRATCH_SIZE];
} caam8_dma_arena_t;

static caam8_dma_arena_t *s_arena;
static uint8_t s_inited;
static uint32_t s_last_jr_status;
static char s_probe_detail[20];

static void caam8_barrier(void)
{
    __DSB();
    __DMB();
}

static void caam8_dma_clean(void)
{
    if (s_arena == NULL)
    {
        return;
    }
    SCB_CleanDCache_by_Addr((uint32_t *)(void *)s_arena, (int32_t)sizeof(*s_arena));
    caam8_barrier();
}

static void caam8_dma_invalidate(void)
{
    if (s_arena == NULL)
    {
        return;
    }
    SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)s_arena, (int32_t)sizeof(*s_arena));
    caam8_barrier();
}

void caam8_clocks_on(void)
{
    CLOCK_EnableClock(kCLOCK_Ipmux1);
    CLOCK_EnableClock(kCLOCK_Ipmux2);
    CLOCK_EnableClock(kCLOCK_Ipmux3);
    CLOCK_EnableClock(kCLOCK_Sec_Debug);
    caam8_barrier();
}

int caam8_probe_readonly(void)
{
    uint32_t irbar;
    uint32_t jrcfg;

    s_probe_detail[0] = '\0';

    /*
     * IRBAR_LO is a plain latch on the JR page. Write a known value and read
     * it back — proves M7 owns JR1 (MID 6). Do not touch JR2 (OP-TEE).
     */
    caam8_wr(CAAM8_JR1_BASE, CAAM8_JR_IRBAR_LO, 0x80080000u);
    caam8_barrier();
    irbar = caam8_rd(CAAM8_JR1_BASE, CAAM8_JR_IRBAR_LO);
    jrcfg = caam8_rd(CAAM8_JR_BASE, CAAM8_JR_JRCFG_LS);

    (void)sprintf(s_probe_detail, "%08lX", (unsigned long)irbar);

    if (irbar == 0x80080000u)
    {
        return 0;
    }
    if ((jrcfg != 0u) && (jrcfg != 0xffffffffu))
    {
        (void)sprintf(s_probe_detail, "%08lX", (unsigned long)jrcfg);
        return 0;
    }
    return CAAM8_ERR_PROBE;
}

const char *caam8_probe_detail(void)
{
    return s_probe_detail;
}

/*
 * Never JRCR-reset the ring. ATF starts JR1 via JRSTARTR on page 0; a reset
 * here would clear that bit with no way for the M7 to set it again.
 */
int caam8_jr_setup_rings(void)
{
    uint32_t irsa;
    uint32_t guard;

    s_last_jr_status = 0;
    s_arena          = (caam8_dma_arena_t *)(uintptr_t)CAAM8_DMA_ARENA;
    (void)memset(s_arena, 0, sizeof(*s_arena));

    /* On i.MX8M: +0 = high half, +4 = low half. */
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_IRBAR_HI, 0u);
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_IRBAR_LO, (uint32_t)(uintptr_t)s_arena->inpring);
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_ORBAR_HI, 0u);
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_ORBAR_LO, (uint32_t)(uintptr_t)s_arena->outring);
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_IRSR, CAAM8_JR_DEPTH);
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_ORSR, CAAM8_JR_DEPTH);
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_JRCFG_LS, caam8_rd(CAAM8_JR_BASE, CAAM8_JR_JRCFG_LS) | CAAM8_JRCFG_IMSK);
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_JRINT, 0x8u); /* clear HALT_COMPLETE if latched */

    guard = 8u;
    while (((caam8_rd(CAAM8_JR_BASE, CAAM8_JR_ORSF) & 0x3FFu) != 0u) && (guard > 0u))
    {
        caam8_wr(CAAM8_JR_BASE, CAAM8_JR_ORJR, 1u);
        caam8_barrier();
        guard--;
    }
    caam8_dma_clean();

    irsa = caam8_rd(CAAM8_JR_BASE, CAAM8_JR_IRSA) & 0x3FFu;
    if (irsa == 0u)
    {
        s_inited = 0;
        return CAAM8_ERR_IRSA;
    }
    s_inited = 1;
    return 0;
}

int caam8_jr_recycle(void)
{
    return caam8_jr_setup_rings();
}

int caam8_jr0_run(uint32_t *desc, uint32_t *jr_status)
{
    uint32_t t;
    uint32_t status;
    uint32_t out_w1;
    uint32_t out_w2;

    if ((!s_inited) || (desc == NULL) || (s_arena == NULL))
    {
        return CAAM8_ERR_ARG;
    }

    if ((caam8_rd(CAAM8_JR_BASE, CAAM8_JR_ORSF) & 0x3FFu) != 0u)
    {
        s_last_jr_status = 0xFFFFFFFEu;
        if (jr_status != NULL)
        {
            *jr_status = s_last_jr_status;
        }
        return CAAM8_ERR_STATUS;
    }

    /* 32-bit pointer layout (validated on silicon as OK-CAAM/M0). */
    s_arena->inpring[0] = (uint32_t)(uintptr_t)desc;
    s_arena->inpring[1] = 0u;
    (void)memset(s_arena->outring, 0, sizeof(s_arena->outring));
    caam8_dma_clean();
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_IRJA, 1u);

    t = CAAM8_JOB_TIMEOUT;
    while (((caam8_rd(CAAM8_JR_BASE, CAAM8_JR_ORSF) & 0x3FFu) == 0u) && (t > 0u))
    {
        t--;
    }
    if (t == 0u)
    {
        s_last_jr_status = 0xFFFFFFFFu;
        if (jr_status != NULL)
        {
            *jr_status = s_last_jr_status;
        }
        return CAAM8_ERR_TIMEOUT;
    }

    caam8_dma_invalidate();
    out_w1           = s_arena->outring[1];
    out_w2           = s_arena->outring[2];
    /* PS=1 puts status one word later; a clean PS=0 run leaves out_w2 at 0. */
    status           = (out_w2 != 0u) ? out_w2 : out_w1;
    s_last_jr_status = status;
    caam8_barrier();
    caam8_wr(CAAM8_JR_BASE, CAAM8_JR_ORJR, 1u);

    if (jr_status != NULL)
    {
        *jr_status = status;
    }
    return (status == 0u) ? 0 : CAAM8_ERR_STATUS;
}

uint8_t *caam8_dma_scratch(void)
{
    return (s_arena != NULL) ? s_arena->scratch : NULL;
}

size_t caam8_dma_scratch_size(void)
{
    return CAAM8_SCRATCH_SIZE;
}

uint32_t *caam8_dma_desc(void)
{
    return (s_arena != NULL) ? s_arena->desc : NULL;
}
