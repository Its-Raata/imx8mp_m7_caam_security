/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * Minimal poll-mode CAAM job-ring 0 driver for i.MX8MP (M7).
 */
#ifndef CAAM_IMX8MP_JR_H_
#define CAAM_IMX8MP_JR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0 = ok */
int caam8_jr0_init(void);

/*
 * Submit a job descriptor (already in CAAM-visible DMA memory) and wait.
 * Returns 0 on JR status success, negative on timeout / JR error.
 * On failure, *jr_status (if non-NULL) receives the out-ring status word.
 */
int caam8_jr0_run(uint32_t *desc, uint32_t *jr_status);

/* Scratch inside the DDR DMA arena (after rings). Valid after init. */
uint8_t *caam8_dma_scratch(void);
size_t caam8_dma_scratch_size(void);

/* Descriptor buffer in the DMA arena. */
uint32_t *caam8_dma_desc(void);
size_t caam8_dma_desc_words(void);

#ifdef __cplusplus
}
#endif

#endif /* CAAM_IMX8MP_JR_H_ */
