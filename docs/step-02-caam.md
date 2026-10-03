# Step 02 — CAAM crypto engine

**Author:** Raata \<its.raata@gmail.com\>  
**Status:** In progress (JR0 driver + ECB KAT; GCM on HW next on EVK)  
**Branch:** `step/02-caam`  
**Tag:** `step-02` (when silicon path works)

## Goal

Same RPMsg protocol and Linux client as Step 01. AES-GCM / HMAC (and later
black blobs) run on **CAAM** instead of `sw_crypto.c`.

## What landed in this branch so far

| Item | Status |
|------|--------|
| `caam_crypto.c` / `.h` — GCM + HMAC API matching `sw_*` | Done |
| `caam_imx8mp_jr.*` — i.MX8MP JR0 poll-mode driver | Done |
| `caam_blob.c` — black blob stub (after GCM proven) | Stub |
| `crypto_service.c` — `M7_USE_CAAM` engine switch | Done |
| CMake flags `M7_USE_CAAM` / `M7_CAAM_HW` (default **0**) | Done |
| Default build = Step 01 behavior (software crypto) | Done |
| Linux CAAM disabled on boot DTB | **Done on EVK** |
| AES-GCM / HMAC parity vs Step 01 on silicon | Next |

### Build flags

```cmake
# armgcc/config.cmake (or -DM7_USE_CAAM=1 -DM7_CAAM_HW=1)
M7_USE_CAAM=0   # 1 = call caam_* for GCM/HMAC
M7_CAAM_HW=0    # 1 = real JR0 + ECB self-test + GCM descriptors
```

With both at 0, the firmware behaves like Step 01.  
With both at 1, UART should print `CAAM: JR0 ready (ECB KAT OK)` before RPMsg announce.

## Platform finding

**You can use CAAM** at `crypto@30900000`. Job rings are at
`0x30901000` / `0x30902000` / `0x30903000` (**0x1000** step).

Do **not** link MCUX `fsl_caam` + RT117x `CAAM_Type` (JR step **0x10000**).
This tree uses a small M7 driver against the i.MX8MP map (`caam_imx8mp_jr.c`).

Descriptors and I/O live in reserved DDR at `0x80080000` (TCM is not
CAAM-visible). Firmware still boots from TCM via `bootaux`.

## Linux ownership (this system)

Linux will **not** use CAAM (SE050 later for app crypto). Apply
[linux/imx8mp-disable-caam.dtsi](../linux/imx8mp-disable-caam.dtsi) to
**`imx8mp-evk-rpmsg.dts`** (the DTB your U-Boot `loadd` uses), rebuild that
DTB, copy to the FAT partition, reboot.

Verified on EVK:

```bash
ls /sys/bus/platform/devices/ | grep -E '3090|jr'   # empty
cat /proc/interrupts | grep -i jr || true           # empty
```

| Ring | Address | Owner |
|------|---------|-------|
| JR0 | 0x30901000 | M7 |
| JR1 | 0x30902000 | unused |
| JR2 | 0x30903000 | unused |

## Soft blob vs black blob

Step 01 soft blobs remain the on-wire format until black-blob wrap is
validated. Then update [PROTOCOL.md](../crypto_rpmsg/doc/PROTOCOL.md).

## Checklist

- [x] Adapter files + `M7_USE_CAAM` switch
- [x] Linux overlay / DTB disables CAAM (verified on board)
- [x] i.MX8MP-specific CAAM JR0 driver (not RT `fsl_caam` map)
- [ ] CAAM init self-test on EVK (`CAAM: JR0 ready`)
- [ ] GCM / HMAC parity vs Step 01 vectors
- [ ] Black blob export/load across M7 restart
- [ ] Tag `step-02` + GitHub Release

## Non-negotiables (unchanged)

1. Do not break `m7_crypto_protocol.h` / `m7_crypto_client`.  
2. Keep software path buildable (`M7_USE_CAAM=0`).  
3. Document JR ownership before claiming Step 02 Done.
