# Changelog

**Author:** Raata <its.raata@gmail.com>

## [Unreleased]

### Added (Step 02 — in progress)

- `caam_crypto.*` / `caam_blob.*` adapters and `M7_USE_CAAM` / `M7_CAAM_HW` build flags
- `caam_imx8mp_jr.*` — i.MX8MP JR0 poll-mode driver + DDR DMA arena at `0x80080000`
- AES-128-ECB NIST KAT in `caam_crypto_init()`; GCM descriptors wired (EVK prove next)
- Default build remains Step 01 software crypto (`M7_USE_CAAM=0`)
- Linux CAAM disable overlay; EVK verified (no `3090*.jr` / JR IRQs)

### Planned

- EVK: `CAAM: JR0 ready`, then GCM/HMAC parity vs Step 01 client
- Black-blob on-wire format after GCM is solid

## [step-01] — 2026-09-27

### Added

- M7 FreeRTOS RPMsg crypto service (software AES-128-GCM, HMAC-SHA256)
- Soft key blobs and Linux `m7_crypto_client` over `/dev/ttyRPMSG30`
- Protocol documentation and project roadmap under `docs/`
