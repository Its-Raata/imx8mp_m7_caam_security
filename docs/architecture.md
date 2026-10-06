# Architecture — i.MX8MP M7 crypto over RPMsg

**Author:** Raata \<its.raata@gmail.com\>  
**Audience:** you (learning path) and anyone reading the GitHub tree  
**Status:** matches `step/02-caam` as of the interactive Linux client

This document explains **how the pieces fit**, not every register bit.
Wire formats live in [PROTOCOL.md](../crypto_rpmsg/doc/PROTOCOL.md).
Milestone notes: [step-01](step-01-software-crypto.md), [step-02](step-02-caam.md).

---

## 1. Why this shape exists

Linux on the Cortex-A53 is convenient for apps and networking. The Cortex-M7
is a smaller, more isolated place to hold keys and run crypto.

Goals:

1. **Keys stay on M7** — Linux sends commands; it does not need the raw AES key
   after `store-aes` (except when you deliberately export a blob).
2. **Stable wire protocol** — swap software crypto for CAAM without rewriting
   the Linux client.
3. **Boot-time ownership** — M7 starts from U-Boot (`bootaux`) before Linux;
   Linux attaches via `remoteproc` + virtio RPMsg and opens `/dev/ttyRPMSG30`.

Later, application-level crypto on Linux can move to **SE050**. CAAM on this
system is reserved for M7 (Linux DTB disables `&crypto` / job rings).

---

## 2. Big picture

```text
┌──────────────────────────────────────────────────────────────────────────┐
│  Cortex-A53  (Linux)                                                     │
│                                                                          │
│   m7_crypto_client  ──raw open/read/write──►  /dev/ttyRPMSG30            │
│         │                                          │                     │
│         │                                          ▼                     │
│         │                              imx_rpmsg_tty / rpmsg_tty         │
│         │                                          │                     │
│         │                                          ▼                     │
│         │                              virtio_rpmsg_bus (virtio0)        │
│         │                                          │                     │
│         └──────── shared DDR vrings / buffers ─────┘                     │
│                    (vdev @ 0x55000000, …)                                │
└──────────────────────────────────┬───────────────────────────────────────┘
                                   │ MU / RPMsg-Lite link
┌──────────────────────────────────▼───────────────────────────────────────┐
│  Cortex-M7  (FreeRTOS)                                                   │
│                                                                          │
│   main.c  app_task                                                       │
│      │  1) RPMsg-Lite remote init + wait link                            │
│      │  2) announce "rpmsg-virtual-tty-channel-1" @ endpoint 30          │
│      │  3) crypto_service_init()  (CAAM JR1 if M7_USE_CAAM)              │
│      │  4) recv → crypto_service_handle → send                           │
│      ▼                                                                   │
│   crypto_service.c     protocol + key slots + CAAM BLOB wrap             │
│      │                                                                   │
│      ├── sw_crypto.*     Step 01 software AES-GCM / HMAC                 │
│      └── caam_crypto.*   Step 02 CAAM (JR1 @ 0x30902000)                 │
│             ├── caam_imx8mp_jr.*   descriptors + DMA in DDR @ 0x80080000│
│             └── caam_blob.*        OTPMK/JDKEK wrap (blob header v2)    │
└──────────────────────────────────────────────────────────────────────────┘
```

Mental model: the client talks to a **TTY that is really an RPMsg channel**.
Everything underneath is shared memory + mailbox, not UART wires.

---

## 3. Boot and ownership sequence

Order matters. If you reverse it, you get “no `/dev/ttyRPMSG*`” or a stuck M7.

```text
U-Boot
  fatload  .bin  →  TCM (0x7e0000) via staging DDR
  bootaux  M7
  fatload  Image + imx8mp-evk-rpmsg.dtb
  booti

Linux
  remoteproc attaches to already-running M7
  virtio RPMsg host comes online
  M7 announces channel  →  creating channel … addr 0x1e
  modprobe rpmsg_tty    →  /dev/ttyRPMSG30
```

| Piece | Who owns it | Notes |
|-------|-------------|--------|
| M7 TCM image | U-Boot `bootaux` | Not started by `echo start > remoteproc…` in the usual flow |
| RPMsg reserved DDR | Device tree | `vdev0vring*`, `vdevbuffer`, `rsc_table` |
| M7 reserved DDR `0x80000000` | Device tree `no-map` | CAAM DMA arena at `0x80080000` (TCM is not CAAM-visible) |
| CAAM block `0x30900000` | **M7 only** | Linux DTB: `&crypto` / `&sec_jr*` `disabled`; U-Boot `mcu_rdc` PDAP/MDA CAAM → DID1 (see `linux/u-boot/`) |
| `/dev/ttyRPMSG30` | `rpmsg_tty` module | Channel can exist in sysfs before the TTY node appears |

---

## 4. Software layers (M7)

| Layer | File(s) | Responsibility |
|-------|---------|----------------|
| Entry / RTOS | `main.c` | Board init, FreeRTOS task, RPMsg loop |
| Protocol | `m7_crypto_protocol.h`, `crypto_service.c` | Parse headers, status codes, key slots |
| Soft blobs | `crypto_service.c` (`M7_USE_CAAM=0`) | Step 01 software wrap (blob v1) |
| Engine switch | `M7_USE_CAAM` in `armgcc/config.cmake` | Route GCM/HMAC/wrap to `sw_*` or `caam_*` |
| Software crypto | `sw_crypto.*` | Step 01 reference |
| CAAM crypto | `caam_crypto.*`, `caam_imx8mp_jr.*` | Step 02 JR1 + descriptors |
| Black blobs | `caam_blob.*` | CAAM BLOB encap/decap (blob header v2) |

`crypto_service_handle()` is the only place that turns a command into crypto.
The RPMsg task never “knows” AES; it only shuttles buffers.

---

## 5. Linux client

File: [`crypto_rpmsg/linux/m7_crypto_client.c`](../crypto_rpmsg/linux/m7_crypto_client.c)

Two ways to use it:

**Interactive menu** (default):

```bash
modprobe rpmsg_tty   # once per boot if needed
cd crypto_rpmsg/linux
gcc -O2 -o m7_crypto_client m7_crypto_client.c
./m7_crypto_client                  # or: ./m7_crypto_client /dev/ttyRPMSG30
```

Menu map:

| # | Action |
|---|--------|
| 1 | Ping |
| 2 | Set / open RPMsg device path |
| 3 | Store AES-128 key |
| 4 | Export AES blob → file |
| 5 | Load AES blob ← file |
| 6 | Encrypt AES-GCM |
| 7 | Decrypt AES-GCM (can reuse last ciphertext) |
| 8–11 | HMAC store / export / load / sign |
| 0 | Quit |

**One-shot CLI** (scripts / CI):

```bash
./m7_crypto_client /dev/ttyRPMSG30 ping
./m7_crypto_client /dev/ttyRPMSG30 store-aes 00112233445566778899aabbccddeeff
```

Important client details:

- Opens the device in **raw** termios mode (command `PING` is `0x0A`; cooked
  tty would corrupt the binary header).
- Drains the driver’s probe banner (`hello world!`) before the first request.
- Speaks only the M7CR header + payload; no line protocol.

---

## 6. Request path (one encrypt)

```text
1. Client builds m7cr_req_hdr_t + ENCRYPT_GCM payload
2. write() to /dev/ttyRPMSG30
3. Linux rpmsg_tty → virtio → shared vring
4. M7 RPMsg-Lite callback / queue wakes app_task
5. crypto_service_handle():
      - check magic/version/length
      - require s_aes_valid
      - ENG_AES_GCM_ENCRYPT(...)   // sw or CAAM
6. Response header + ct||tag
7. Client read() → print hex
```

Status `5` = no key stored. Status `6` = crypto engine failed (e.g. CAAM job).
Status `0` = success.

---

## 7. CAAM path (Step 02)

Linux must not bind job rings (verified empty `3090*.jr` / no JR IRQs).

On M7, with `M7_USE_CAAM=1` and `M7_CAAM_HW=1`:

1. ATF starts JR1 (`JRSTART` bit 1) and locks `JR1MID = LDID|6`. Page 0 is
   unreachable from the M7, so the ring must already be started.
2. First `ping` programs JR1 rings in reserved DDR (`0x80080000`) and runs
   NIST AES-128-ECB, AES-GCM TC2, RFC 4231 HMAC-SHA256, and CAAM BLOB KATs.
3. Later GCM / HMAC commands use the same single-job descriptors.

We intentionally **do not** use MCUX `fsl_caam` + RT117x `CAAM_Type` (wrong JR
spacing). See [step-02-caam.md](step-02-caam.md).

---

## 8. Soft blob vs black blob

| Kind | Where | Purpose |
|------|--------|---------|
| Soft blob (v1) | `crypto_service.c` when `M7_USE_CAAM=0` | Software AES-CBC + HMAC wrap |
| Black blob (v2) | `caam_blob.c` on JR1 | CAAM BLOB protocol; wrap key is OTPMK/JDKEK |

CAAM name is **red blob** (plaintext in, blob out). This project calls it
black blob because the wrap is hardware-bound, not a software constant.
The blob will not unwrap on another chip. Keys still sit in M7 RAM after
load — they are not kept as in-register CAAM black keys.

---

## 9. File map (where to look when learning)

```text
main.c                         boot → RPMsg → command loop
crypto_service.c               protocol state machine
m7_crypto_protocol.h           magic, cmds, status, sizes
sw_crypto.c / caam_crypto.c    engines behind the same API
caam_blob.c                    CAAM BLOB wrap / unwrap
caam_imx8mp_jr.c               i.MX8MP job-ring driver
linux/imx8mp-disable-caam.dtsi include into imx8mp-evk-rpmsg.dts
crypto_rpmsg/linux/            host client (menu + CLI)
crypto_rpmsg/doc/PROTOCOL.md   on-the-wire bytes
m7_crypto_protocol.h           shared by firmware and the Linux client
docs/step-0x-*.md              milestone checklists
```

---

## 10. Learning checklist (suggested order)

1. Boot flow: U-Boot `bootaux` → Linux attach → `modprobe rpmsg_tty`.
2. Menu **1 Ping** — proves endpoint + protocol header.
3. Menu **3 Store AES** then **6 Encrypt** / **7 Decrypt** — key slot + GCM.
4. Menu **4 / 5** — CAAM blob round-trip across M7 reset (same blob file).
5. Read `crypto_service.c` `ENCRYPT_GCM` case alongside PROTOCOL.md.
6. Read `caam_imx8mp_jr.c` enqueue/dequeue; compare to Linux `jr.c` mentally.
7. Confirm engine via **ping** payload: `OK-CAAM` / `OK-SW` / `FAIL-*`
   (custom boards often have no usable M7 UART4).

When you are ready, we walk these steps one by one with questions — not just
commands.
