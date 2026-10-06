# CAAM JR1 ownership — hand the ring to the M7

## Apply these two (in order)

| Patch | What it does |
|-------|----------------|
| `0005-imx8m-caam-JR1-MID6-M7.patch` | Lock `JR1MID = LDID \| 6` (Cortex-M7 AIPSTZ master ID). Leave JR2 for OP-TEE. |
| `0007-imx8m-caam-start-jr1.patch` | Stop JR1 cleanly, set DIDs, then start rings (`JRSTARTR`). Without this, jobs “complete” but the DECO never runs. |

Also apply the U-Boot RDC grant: [`../u-boot/`](../u-boot/).

Obsolete experiments (do **not** flash): `0001`…`0004` (wrong MID), `0006` (CAAM MDA→DID1 — not required).

## Why MID 6, not RDC DID 1

CAAM `JRaDID.PRIM_DID` matches the **AIPSTZ master ID**, not the RDC domain:

| Master     | PRIM_DID |
|------------|----------|
| EDMA       | 0        |
| Cortex-A53 | 1        |
| Cortex-M7  | **6**    |

Stock ATF leaves JR1 at MID 1, so M7 MMIO of `0x30902000` reads as zero.

## Why JRSTART matters

With CAAM virtualization on, `JRSTARTR` (page 0, `+0x5c`) gates execution.
An unstarted ring still accepts jobs and reports completions, but the DECO
never reads the descriptor. Symptom: every job returns the same canned
`JRSTA`, output slot untouched. Page 0 is invisible from the M7, so BL31
must start the ring.

`0007` polls `JRCR` to completion (the previous write was dropped because
reset was never waited for), clears `JRINT` HALT_COMPLETE, sets DIDs, then
writes `JRSTARTR = 0xf`. On this part expect boot NOTICE `JRSTART=0x7`
(JR0–JR2; no JR3).

## Flash (eMMC boot0)

```bash
# after bitbake imx-atf + imx-boot
strings imx-boot-*-flash_evk | grep "CAAM MCFGR"   # must hit

# on the board — seek=0 ONLY (seek=32 can brick). Keep boot0-bak.img.
echo 0 > /sys/block/mmcblk2boot0/force_ro
dd if=imx-boot-...-flash_evk of=/dev/mmcblk2boot0 bs=1K seek=0 conv=fsync
echo 1 > /sys/block/mmcblk2boot0/force_ro
```

Boot NOTICE should show `JR1DID=0x80000006` and `JRSTART` with bit 1 set.
Then M7 ping → `OK-CAAM`.

## M7 driver notes

- Target JR1 at `0x30902000`.
- Ring base registers are most-significant-half-first (`+0` high, `+4` low).
- Never JRCR-reset from the M7 — that clears JRSTART with no recovery.
