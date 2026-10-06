# i.MX8MP M7 crypto service (RPMsg)

**Author:** Raata \<its.raata@gmail.com\> ([Its-Raata](https://github.com/Its-Raata))  
**Board:** NXP i.MX8MP EVK · **Remote core:** Cortex-M7 (FreeRTOS + RPMsg-Lite)  
**Host:** Linux Cortex-A53 · **Channel:** `/dev/ttyRPMSG30`

Linux sends key, encrypt, decrypt, and sign requests to the M7. Keys stay on
the M7; Linux receives ciphertext or a MAC. The wire protocol is stable so each
roadmap step can swap the crypto engine without rewriting the userspace client.

| Status | Milestone |
|--------|-----------|
| **Done** | [Step 1 — Software crypto + soft blobs](docs/step-01-software-crypto.md) |
| **Done** | [Step 2 — CAAM AES-GCM / HMAC + black blobs](docs/step-02-caam.md) (`step-02`) |
| Planned | Step 3 — Hardening (IV policy, key lifecycle, demo scripts) |

Full plan: [docs/ROADMAP.md](docs/ROADMAP.md) ·  
**Architecture (learn here):** [docs/architecture.md](docs/architecture.md) ·  
Protocol: [crypto_rpmsg/doc/PROTOCOL.md](crypto_rpmsg/doc/PROTOCOL.md)

---

## Architecture (summary)

See **[docs/architecture.md](docs/architecture.md)** for the full boot path, layer
map, CAAM ownership, and learning checklist.

```text
Linux: m7_crypto_client  →  /dev/ttyRPMSG30  →  virtio RPMsg
M7:    RPMsg-Lite (@30)  →  crypto_service  →  sw_crypto / CAAM
```

---

## Quick demo

Boot M7 from U-Boot **before** Linux (`imx8mp-evk-rpmsg.dtb`). Details:
[docs/step-01-software-crypto.md](docs/step-01-software-crypto.md).

```bash
modprobe rpmsg_tty    # or: modprobe imx_rpmsg_tty
cd crypto_rpmsg/linux && gcc -O2 -o m7_crypto_client m7_crypto_client.c

# Interactive menu (recommended)
./m7_crypto_client /dev/ttyRPMSG30

# Or one-shot CLI
./m7_crypto_client /dev/ttyRPMSG30 ping
```

Menu: **1** ping · **2** set channel · **3** store AES · **6/7** encrypt/decrypt · **0** quit.

---

## Build

MCUXpresso SDK (MIMX8ML8), CMake preset `debug`:

```bash
export SdkRootDirPath=/path/to/mcuxsdk-aarch64
cmake --preset debug -S armgcc
cmake --build armgcc/debug
```

Output: `armgcc/debug/imx8mp_m7_caam_security.bin`

---

## Repository layout

```text
main.c, crypto_service.*, sw_crypto.*, m7_crypto_protocol.h   # M7 application
armgcc/                                                         # CMake / linker
crypto_rpmsg/linux/                                             # userspace client
crypto_rpmsg/doc/PROTOCOL.md                                    # wire format
docs/                                                           # roadmap & milestones
```

Git tags mark frozen milestones (`step-01`, …) so each stage stays readable on GitHub.

---

## License

MIT — see [LICENSE](LICENSE).
