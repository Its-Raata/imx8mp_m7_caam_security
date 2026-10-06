# U-Boot — grant CAAM to M7 (RDC)

Patch: `0001-imx8mp-mcu_rdc-grant-CAAM-to-M7.patch`

Adds CAAM to U-Boot `mcu_rdc` so ATF opens the peripheral for domain 1 when
`bootaux` runs the M7. Also corrects the i.MX8MP CAAM PDAP index (**58**, not
114 from 8MN/MM).

This is necessary alongside the ATF JR1 ownership patches in
[`../atf/`](../atf/). RDC alone does not make JR1 usable — CAAM filters on
AIPSTZ master ID 6, which only BL31 can lock into `JR1MID`.

```bash
# in U-Boot source
patch -p1 < 0001-imx8mp-mcu_rdc-grant-CAAM-to-M7.patch
# bitbake -c cleansstate u-boot-imx imx-boot && bitbake imx-boot
# flash boot0 at seek=0 only; keep boot0-bak.img
```
