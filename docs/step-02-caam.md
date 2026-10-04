# Step 02 — CAAM crypto engine

**Author:** Raata \<its.raata@gmail.com\>  
**Status:** Done on EVK — JR1 + AES-GCM / HMAC-SHA256 over RPMsg (`OK-CAAM`)  
**Branch:** `step/02-caam`

## Goal

Same RPMsg protocol and Linux client as Step 01. AES-GCM / HMAC run on
**CAAM JR1** instead of `sw_crypto.c`. Soft-blob wrap stays Step-01 format.

## What landed

| Item | Status |
|------|--------|
| `caam_crypto.c` — GCM + HMAC + NIST/RFC KATs on first ping | Done |
| `caam_imx8mp_jr.*` — i.MX8MP JR1 poll-mode driver | Done |
| ATF `0005` + `0007` — JR1 MID=6, ring started | Done |
| U-Boot `mcu_rdc` CAAM → DID1 | Done |
| Linux DTB disables `&crypto` / job rings | Done |
| Soft blobs (Step 01 format) | Unchanged |
| Black blob export | Next |

### Build flags

```cmake
# armgcc/config.cmake
M7_USE_CAAM=1     # route GCM/HMAC to caam_*
M7_CAAM_HW=1      # real JR1 + descriptors
M7_CAAM_TOUCH=1   # first ping brings CAAM up (0 = never MMIO)
```

```bash
./m7_crypto_client /dev/ttyRPMSG30 ping
# expect: payload=OK-CAAM
```

## Platform

| Ring | Address    | Owner                          |
|------|------------|--------------------------------|
| JR0  | 0x30901000 | HAB (do not use)               |
| JR1  | 0x30902000 | **M7** (MID 6, started by ATF) |
| JR2  | 0x30903000 | OP-TEE                         |

Descriptors and I/O live in reserved DDR at `0x80080000`.

Firmware patches: [linux/atf/](../linux/atf/), [linux/u-boot/](../linux/u-boot/).

## Checklist

- [x] Adapter files + `M7_USE_CAAM` switch
- [x] Linux overlay / DTB disables CAAM
- [x] i.MX8MP JR1 driver (not RT `fsl_caam` map)
- [x] U-Boot `mcu_rdc` CAAM → DID1
- [x] ATF JR1 MID=6 + JRSTART
- [x] Ping `OK-CAAM` (ECB + GCM + HMAC KATs)
- [x] Encrypt/decrypt GCM and HMAC sign on silicon
- [ ] Black blob export/load across M7 restart
- [ ] Tag `step-02` + GitHub Release

## Non-negotiables

1. Do not break `m7_crypto_protocol.h` / `m7_crypto_client`.
2. Keep software path buildable (`M7_USE_CAAM=0`).
3. Document JR ownership before claiming Step 02 Done.
