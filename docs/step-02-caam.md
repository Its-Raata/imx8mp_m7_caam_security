# Step 02 — CAAM crypto engine

**Author:** Raata \<its.raata@gmail.com\>  
**Status:** In progress (API + build switch landed; hardware bring-up next)  
**Branch:** `step/02-caam`  
**Tag:** `step-02` (when silicon path works)

## Goal

Same RPMsg protocol and Linux client as Step 01. AES-GCM / HMAC (and later
black blobs) run on **CAAM** instead of `sw_crypto.c`.

## What landed in this branch so far

| Item | Status |
|------|--------|
| `caam_crypto.c` / `.h` — GCM + HMAC API matching `sw_*` | Done (scaffold) |
| `caam_blob.c` / `.h` — black blob API | Done (scaffold) |
| `crypto_service.c` — `M7_USE_CAAM` engine switch | Done |
| CMake flags `M7_USE_CAAM` / `M7_CAAM_HW` (default **0**) | Done |
| Default build = Step 01 behavior (software crypto) | Done |
| Link `fsl_caam` + `CAAM_Type` for MIMX8ML8 | **Next** |
| Reserve Linux job ring for M7 | **Next** (see below) |
| EVK parity test with client | After HW |

### Build flags

```cmake
# armgcc/config.cmake (or -DM7_USE_CAAM=1)
M7_USE_CAAM=0   # 1 = call caam_* for GCM/HMAC
M7_CAAM_HW=0    # 1 = compile real fsl_caam calls (needs device CAAM_Type)
```

With both at 0, the firmware behaves like Step 01.  
With `M7_USE_CAAM=1` and `M7_CAAM_HW=0`, GCM/HMAC return crypto errors until HW is linked (wiring check).

## Platform finding (important)

MCUXpresso **MIMX8ML8** headers expose CAAM IRQs and RDC IDs, but **do not**
define `CAAM_Type` / `CAAM_BASE` the way RT1170 does. The NXP `fsl_caam` driver
expects those symbols.

So Step 02 has two layers:

1. **Software architecture** (this commit) — stable switch, same protocol.  
2. **Silicon bring-up** — add `caam_imx8mp_device.h` (base `0x30900000` per RM),
   clock/RDC access from M7, link `fsl_caam.c`, set `M7_CAAM_HW=1`.

## Linux job-ring ownership

Linux `caam`/`jr` nodes normally claim the job rings. M7 must use a ring Linux
does **not** bind.

Typical approach on EVK:

1. In the RPMsg DT overlay / `imx8mp-evk-rpmsg.dts`, disable or remove one `jr@…`
   node reserved for M7 (often JR2 or JR3 — confirm against your kernel DT).
2. Confirm with `cat /proc/interrupts | grep jr` that the reserved ring stays idle
   under Linux crypto load.
3. Point M7 `caam_handle_t.jobRing` at that ring (`kCAAM_JobRing2` / `3`).

Document the chosen ring here when fixed:

| Ring | Owner |
|------|--------|
| JR0 | Linux (default) |
| JR1 | Linux (default) |
| JR? | **M7 (TBD)** |

## Soft blob vs black blob

Step 01 soft blobs remain the on-wire format until `M7_CAAM_HW` black-blob wrap
is validated. Then update [PROTOCOL.md](../crypto_rpmsg/doc/PROTOCOL.md) with
the CAAM blob layout (`keymod || caam_blob`).

## Checklist

- [x] Branch `step/02-caam`
- [x] Adapter files + `M7_USE_CAAM` switch
- [ ] `caam_imx8mp_device.h` + link `fsl_caam.c`
- [ ] CAAM init self-test on EVK
- [ ] GCM / HMAC parity vs Step 01 vectors
- [ ] Black blob export/load across M7 restart
- [ ] Linux JR reserved and documented
- [ ] Tag `step-02` + GitHub Release

## Non-negotiables (unchanged)

1. Do not break `m7_crypto_protocol.h` / `m7_crypto_client`.  
2. Keep software path buildable (`M7_USE_CAAM=0`).  
3. Document JR ownership before claiming Step 02 Done.
