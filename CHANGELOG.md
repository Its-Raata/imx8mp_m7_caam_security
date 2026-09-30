# Changelog

**Author:** Raata <its.raata@gmail.com>

## [Unreleased]

### Added (Step 02 — in progress)

- `caam_crypto.*` / `caam_blob.*` adapters and `M7_USE_CAAM` / `M7_CAAM_HW` build flags
- Default build remains Step 01 software crypto (`M7_USE_CAAM=0`)

### Planned

- Link `fsl_caam` for MIMX8ML8, reserve Linux job ring, black-blob on-wire format

## [step-01] — 2026-09-27

### Added

- M7 FreeRTOS RPMsg crypto service (software AES-128-GCM, HMAC-SHA256)
- Soft key blobs and Linux `m7_crypto_client` over `/dev/ttyRPMSG30`
- Protocol documentation and project roadmap under `docs/`
