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
| **Next** | [Step 2 — CAAM AES-GCM / HMAC + black blobs](docs/step-02-caam.md) |
| Planned | Step 3 — Hardening (IV policy, key lifecycle, demo scripts) |

Full plan: [docs/ROADMAP.md](docs/ROADMAP.md) · Protocol: [crypto_rpmsg/doc/PROTOCOL.md](crypto_rpmsg/doc/PROTOCOL.md)

---

## Architecture (Step 1)

```text
Linux userspace                         Cortex-M7 (FreeRTOS)
m7_crypto_client                        main.c  →  app_task
        |                                 RPMsg-Lite (endpoint 30)
        v                                 v
/dev/ttyRPMSG30  <--- virtio RPMsg --->  rpmsg-virtual-tty-channel-1
        |                                 |
        +---- shared vring 0x55000000 --->  crypto_service.c
                                              │
                                    ┌─────────┴─────────┐
                                    │ Step 1: sw_crypto │
                                    │ Step 2: CAAM      │  ← same API surface
                                    └───────────────────┘
```

| Layer | Role | Files |
|-------|------|--------|
| Pipe | RPMsg recv / send | `main.c` |
| Commands | Protocol + key slots | `crypto_service.c`, `m7_crypto_protocol.h` |
| Crypto engine | AES-GCM, HMAC, wrap | `sw_crypto.c` → later CAAM |
| Host test | Raw tty client | `crypto_rpmsg/linux/m7_crypto_client.c` |

---

## Quick demo (Step 1)

Boot M7 from U-Boot **before** Linux (`imx8mp-evk-rpmsg.dtb`). Details:
[docs/step-01-software-crypto.md](docs/step-01-software-crypto.md).

```bash
modprobe imx_rpmsg_tty
cd crypto_rpmsg/linux && gcc -O2 -o m7_crypto_client m7_crypto_client.c
export DEV=/dev/ttyRPMSG30
./m7_crypto_client $DEV ping
./m7_crypto_client $DEV store-aes 00112233445566778899aabbccddeeff
./m7_crypto_client $DEV encrypt-gcm 000000000000000000000000 "" "hello"
./m7_crypto_client $DEV decrypt-gcm 000000000000000000000000 "" "<ct+tag-hex>"
```

`status=0 payload=OK` means the channel and protocol are alive.

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
