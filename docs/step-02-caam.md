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

## Platform finding (do not skip)

**You can use CAAM.** The i.MX8MP has it at `crypto@30900000` with job rings
`jr@1000`, `jr@2000`, `jr@3000`.

**You cannot drop in the SDK `fsl_caam.c` as-is.** That driver and `CAAM_Type`
in MCUXpresso are for i.MX RT117x, where job-ring registers sit at a **different
offset** (`0x10000` step) than i.MX8MP (`0x1000` / `0x2000` / `0x3000`).
Pointing that type at `0x30900000` would program the wrong addresses.

Next hardware slice: a small M7 driver written against the **i.MX8MP**
reference-manual map (or an NXP package that already targets 8M), using
**job ring 0** (`0x30901000`).

## Linux ownership (this system)

Linux will **not** use CAAM. Application crypto later is **SE050**.
The kernel still binds the driver unless you disable the node.

In this tree’s `imx8mp.dtsi`, `sec_jr0` is already `disabled`, but **`sec_jr1`
and `sec_jr2` are enabled** — Linux can still run CAAM jobs there.

Apply [linux/imx8mp-disable-caam.dtsi](../linux/imx8mp-disable-caam.dtsi)
so `&crypto` and all three rings are `disabled`, rebuild the DTB, and boot that
image. Check:

```bash
ls /proc/device-tree/soc@0/bus@30000000/crypto@30900000/status
# expect "disabled", or the node absent from the booted tree
cat /proc/interrupts | grep -i jr || true
```

No JR interrupts under Linux load means M7 can own the block.

| Ring | Address | Owner after overlay |
|------|---------|---------------------|
| JR0 | 0x30901000 | M7 |
| JR1 | 0x30902000 | unused |
| JR2 | 0x30903000 | unused |

## Soft blob vs black blob

Step 01 soft blobs remain the on-wire format until `M7_CAAM_HW` black-blob wrap
is validated. Then update [PROTOCOL.md](../crypto_rpmsg/doc/PROTOCOL.md) with
the CAAM blob layout (`keymod || caam_blob`).

## Checklist

- [x] Adapter files + `M7_USE_CAAM` switch
- [x] Linux overlay that disables CAAM (`linux/imx8mp-disable-caam.dtsi`)
- [ ] i.MX8MP-specific CAAM job-ring driver (not RT `fsl_caam` register map)
- [ ] CAAM init self-test on EVK
- [ ] GCM / HMAC parity vs Step 01 vectors
- [ ] Black blob export/load across M7 restart
- [ ] Tag `step-02` + GitHub Release

## Non-negotiables (unchanged)

1. Do not break `m7_crypto_protocol.h` / `m7_crypto_client`.  
2. Keep software path buildable (`M7_USE_CAAM=0`).  
3. Document JR ownership before claiming Step 02 Done.
