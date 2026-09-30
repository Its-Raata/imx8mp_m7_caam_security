# Step 01 — Software crypto over RPMsg

**Status:** Done  
**Tag:** `step-01`  
**Goal:** Prove Linux ↔ M7 crypto path end-to-end before touching CAAM.

## What this step delivers

- FreeRTOS M7 firmware loaded from U-Boot TCM (`bootaux 0x7e0000`)
- RPMsg name service: `rpmsg-virtual-tty-channel-1` → `/dev/ttyRPMSG30`
- Binary protocol (`M7CR`): ping, store/load/export AES & HMAC, AES-128-GCM, HMAC-SHA256
- Software crypto in `sw_crypto.c`
- Soft blobs (AES-CBC wrap + HMAC) so Linux can save/reload keys
- Userspace client: `crypto_rpmsg/linux/m7_crypto_client.c` (raw tty)

## Done criteria (checked on EVK)

- [x] `ping` → `status=0 payload=OK`
- [x] `store-aes` + `encrypt-gcm` + `decrypt-gcm` round-trip
- [x] Ciphertext verifiable on another machine with the same key/IV/AAD (interop)

## Key files

| File | Role |
|------|------|
| `main.c` | Board bring-up, RPMsg loop |
| `crypto_service.c` | Commands + soft blob |
| `sw_crypto.c` | AES-GCM / SHA-256 / HMAC / CBC |
| `m7_crypto_protocol.h` | Shared wire format |
| `crypto_rpmsg/linux/m7_crypto_client.c` | Host test tool |

## Boot sketch

Use `imx8mp-evk-rpmsg.dtb`. Load `.bin` to TCM via U-Boot, then boot Linux.
See root [README.md](../README.md) for the `m7load` / `m7copy` / `m7aux` env block.

## Limits (honest)

- Soft blob wrap key is a **fixed prototype** constant — not OTPMK / CAAM black blob
- IV in demos may be all-zero; production must use unique IVs per message
- Payload size limited by RPMsg buffer (~400 bytes plaintext+AAD)
- `remoteproc` ELF start is not the supported path for this image (U-Boot early boot is)

## Next

→ [Step 02 — CAAM](step-02-caam.md)
