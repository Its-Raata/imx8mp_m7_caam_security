/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * i.MX8MP CAAM JR0: reset, program rings in reserved DDR, poll-mode run.
 */
#include "caam_imx8mp_jr.h"
#include "caam_imx8mp_regs.h"

#include "board.h"
#include "fsl_common.h"
#include "fsl_rdc.h"

#include <string.h>

#define CAAM8_JR_DEPTH       (4u)
#define CAAM8_DESC_WORDS     (64u)
#define CAAM8_SCRATCH_SIZE   (1024u)
#define CAAM8_JOB_TIMEOUT    (2000000u)

/*
 * Arena layout (32-bit CAAM pointer mode):
 *   inpring[DEPTH]           4 * 4
 *   outring[DEPTH]{desc,sts} 4 * 8
 *   desc[DESC_WORDS]
 *   scratch[SCRATCH_SIZE]
 */
typedef struct
{
    uint32_t inpring[CAAM8_JR_DEPTH];
    struct
    {
        uint32_t desc;
        uint32_t status;
    } outring[CAAM8_JR_DEPTH];
    uint32_t desc[CAAM8_DESC_WORDS];
    uint8_t scratch[CAAM8_SCRATCH_SIZE];
} caam8_dma_arena_t;

static caam8_dma_arena_t *s_arena;
static uint32_t s_head;
static uint32_t s_tail;
static uint8_t s_inited;

static void caam8_barrier(void)
{
    __DSB();
    __DMB();
}

static void caam8_rdc_open(void)
{
    rdc_periph_access_config_t cfg;
    uint8_t domainId = RDC_GetCurrentMasterDomainId(RDC);

    if ((0x1U & RDC_GetPeriphAccessPolicy(RDC, kRDC_Periph_RDC, domainId)) == 0U)
    {
        return;
    }

    RDC_GetDefaultPeriphAccessConfig(&cfg);
    cfg.periph = kRDC_Periph_CAAM;
    cfg.policy = (uint16_t)(RDC_ACCESS_POLICY(0U, kRDC_ReadWrite) | RDC_ACCESS_POLICY(1U, kRDC_ReadWrite));
    RDC_SetPeriphAccessConfig(RDC, &cfg);
}

static int caam8_jr_reset(void)
{
    uint32_t t = CAAM8_JOB_TIMEOUT;

    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_JRCFG_LS, caam8_rd(CAAM8_JR0_BASE, CAAM8_JR_JRCFG_LS) | CAAM8_JRCFG_IMSK);
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_JRCR, CAAM8_JRCR_RESET);
    while (((caam8_rd(CAAM8_JR0_BASE, CAAM8_JR_JRCR) & CAAM8_JRCR_RESET) != 0u) && (t > 0u))
    {
        t--;
    }
    return (t == 0u) ? -1 : 0;
}

int caam8_jr0_init(void)
{
    uint32_t irsa;
    uint32_t mcfgr;

    s_inited = 0;
    s_head   = 0;
    s_tail   = 0;
    s_arena  = (caam8_dma_arena_t *)(uintptr_t)CAAM8_DMA_ARENA;
    (void)memset(s_arena, 0, sizeof(*s_arena));

    caam8_rdc_open();

    /* Prefer 32-bit descriptor/ring pointers (DDR arena is below 4G). */
    mcfgr = caam8_rd(CAAM8_BASE, CAAM8_MCFGR_OFF);
    if ((mcfgr & CAAM8_MCFGR_LONG_PTR) != 0u)
    {
        caam8_wr(CAAM8_BASE, CAAM8_MCFGR_OFF, mcfgr & ~CAAM8_MCFGR_LONG_PTR);
    }

    /* Start JR0 if the controller exposes JRSTART. */
    caam8_wr(CAAM8_BASE, CAAM8_JRSTART_OFF, caam8_rd(CAAM8_BASE, CAAM8_JRSTART_OFF) | CAAM8_JRSTART_JR0);

    if (caam8_jr_reset() != 0)
    {
        return -1;
    }

    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_IRBAR_LO, (uint32_t)(uintptr_t)s_arena->inpring);
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_IRBAR_HI, 0u);
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_ORBAR_LO, (uint32_t)(uintptr_t)s_arena->outring);
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_ORBAR_HI, 0u);
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_IRSR, CAAM8_JR_DEPTH);
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_ORSR, CAAM8_JR_DEPTH);

    /* Keep interrupts masked; we poll ORSF. */
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_JRCFG_LS, caam8_rd(CAAM8_JR0_BASE, CAAM8_JR_JRCFG_LS) | CAAM8_JRCFG_IMSK);

    caam8_barrier();
    irsa = caam8_rd(CAAM8_JR0_BASE, CAAM8_JR_IRSA) & 0x3FFu;
    if (irsa == 0u)
    {
        return -2;
    }

    s_inited = 1;
    return 0;
}

int caam8_jr0_run(uint32_t *desc, uint32_t *jr_status)
{
    uint32_t t;
    uint32_t status;
    uint32_t out_desc;

    if ((!s_inited) || (desc == NULL) || (s_arena == NULL))
    {
        return -1;
    }

    s_arena->inpring[s_head] = (uint32_t)(uintptr_t)desc;
    s_head                   = (s_head + 1u) & (CAAM8_JR_DEPTH - 1u);
    caam8_barrier();
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_IRJA, 1u);

    t = CAAM8_JOB_TIMEOUT;
    while (((caam8_rd(CAAM8_JR0_BASE, CAAM8_JR_ORSF) & 0x3FFu) == 0u) && (t > 0u))
    {
        t--;
    }
    if (t == 0u)
    {
        if (jr_status != NULL)
        {
            *jr_status = 0xFFFFFFFFu;
        }
        return -2;
    }

    out_desc = s_arena->outring[s_tail].desc;
    status   = s_arena->outring[s_tail].status;
    (void)out_desc;
    s_tail = (s_tail + 1u) & (CAAM8_JR_DEPTH - 1u);
    caam8_barrier();
    caam8_wr(CAAM8_JR0_BASE, CAAM8_JR_ORJR, 1u);

    if (jr_status != NULL)
    {
        *jr_status = status;
    }
    return (status == 0u) ? 0 : -3;
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

size_t caam8_dma_desc_words(void)
{
    return CAAM8_DESC_WORDS;
}
