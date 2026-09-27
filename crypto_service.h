#ifndef CRYPTO_SERVICE_H_
#define CRYPTO_SERVICE_H_

#include "m7_crypto_protocol.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_service_init(void);

uint32_t crypto_service_handle(const uint8_t *req, uint32_t req_len, uint8_t *rsp, uint32_t rsp_cap);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO_SERVICE_H_ */
