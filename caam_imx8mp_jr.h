/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * Minimal poll-mode CAAM job-ring driver for i.MX8MP (M7).
 * Uses JR1 in hardware (JR0 reserved for HAB); API keeps jr0_* names.
 */
#ifndef CAAM_IMX8MP_JR_H_
#define CAAM_IMX8MP_JR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAAM8_ERR_PROBE   (-10)
#define CAAM8_ERR_IRSA    (-13)
#define CAAM8_ERR_ARG     (-1)
#define CAAM8_ERR_TIMEOUT (-2)
#define CAAM8_ERR_STATUS  (-3)

void caam8_clocks_on(void);
/* Read-only JR window check. 0 = JR1 looks alive. */
int caam8_probe_readonly(void);
const char *caam8_probe_detail(void);

/* Program IR/OR bases in DDR; drain stale output slots. Never JRCR-reset. */
int caam8_jr_setup_rings(void);
int caam8_jr_recycle(void);

int caam8_jr0_run(uint32_t *desc, uint32_t *jr_status);

uint8_t *caam8_dma_scratch(void);
size_t caam8_dma_scratch_size(void);
uint32_t *caam8_dma_desc(void);

#ifdef __cplusplus
}
#endif

#endif /* CAAM_IMX8MP_JR_H_ */
