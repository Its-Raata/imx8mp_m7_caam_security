# Step 02 — CAAM crypto engine + black blobs

**Author:** Raata \<its.raata@gmail.com\>  
**Status:** Done  
**Tag:** `step-02`  
**Branch:** `step/02-caam`

## Goal

Same RPMsg protocol and Linux client as Step 01. AES-GCM / HMAC run on
**CAAM JR1**. STORE/LOAD/EXPORT wrap keys with the **CAAM BLOB** protocol
(OTPMK / JDKEK) instead of the Step-01 software wrap key.

## What landed

| Item | Status |
|------|--------|
| `caam_crypto.c` — GCM + HMAC + NIST/RFC KATs on first ping | Done |
| `caam_imx8mp_jr.*` — i.MX8MP JR1 poll-mode driver | Done |
| `caam_blob.c` — CAAM red-blob encap/decap (project: black blob) | Done |
| ATF `0005` + `0007` — JR1 MID=6, ring started | Done |
| U-Boot `mcu_rdc` CAAM → DID1 | Done |
| Linux DTB disables `&crypto` / job rings | Done |
| Blob header v2 — same commands, new payload bytes | Done |

### Build flags

```cmake
# armgcc/config.cmake
M7_USE_CAAM=1     # route GCM/HMAC/wrap to caam_*
M7_CAAM_HW=1      # real JR1 + descriptors
M7_CAAM_TOUCH=1   # first ping brings CAAM up (0 = never MMIO)
```

```bash
./m7_crypto_client /dev/ttyRPMSG30 ping
# expect: payload=OK-CAAM   (ECB + GCM + HMAC + blob KATs)

./m7_crypto_client /dev/ttyRPMSG30 store-aes --hex 00112233445566778899aabbccddeeff
# expect: blob length 92  (28-byte header + 16 + 48)
```

## Platform

| Ring | Address    | Owner                          |
|------|------------|--------------------------------|
| JR0  | 0x30901000 | HAB (do not use)               |
| JR1  | 0x30902000 | **M7** (MID 6, started by ATF) |
| JR2  | 0x30903000 | OP-TEE                         |

Descriptors and I/O live in reserved DDR at `0x80080000`.

Firmware patches: [linux/atf/](../linux/atf/), [linux/u-boot/](../linux/u-boot/).

## Done criteria (checked on EVK)

- [x] Adapter files + `M7_USE_CAAM` switch
- [x] Linux overlay / DTB disables CAAM
- [x] i.MX8MP JR1 driver (not RT `fsl_caam` map)
- [x] U-Boot `mcu_rdc` CAAM → DID1
- [x] ATF JR1 MID=6 + JRSTART (`JR1DID=0x80000006`, `JRSTART=0x7`)
- [x] Ping `OK-CAAM` (ECB + GCM + HMAC KATs)
- [x] Encrypt/decrypt GCM and HMAC sign on silicon
- [x] CAAM BLOB wrap/unwrap replaces software wrap (`FAIL-BLOB` if KAT fails)
- [x] Tag `step-02` + GitHub Release

## Limits (honest)

- Blob v2 will not unwrap on another chip, or on `M7_USE_CAAM=0` firmware
- Step-01 v1 soft-blob files will not load on this firmware
- Keys still sit in M7 RAM after unwrap (not kept as CAAM black keys in-register)
- M7 cannot start or reassign JR1; ATF must do that (page 0 is invisible from M7)
- Do not JRCR-reset JR1 from the M7 (clears JRSTART with no recovery)
- Payload size still limited by RPMsg (~400 bytes plaintext+AAD)

## Next

→ Step 03 — Hardening (IV policy, key lifecycle, demo scripts)
