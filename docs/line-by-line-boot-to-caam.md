# Line by line: from `main()` to hardware CAAM

**Author:** Raata \<its.raata@gmail.com\>  
**Audience:** learning this tree after Step 02 is done  
**Companion docs:** [architecture.md](architecture.md) · [PROTOCOL.md](../crypto_rpmsg/doc/PROTOCOL.md) · [step-02-caam.md](step-02-caam.md)

This guide walks **execution order** on the M7: power-on → FreeRTOS → RPMsg →
first `PING` → CAAM JR1 alive (`OK-CAAM`).

The Linux client and software crypto (`sw_crypto.c`) are summarized only where
they touch this path. Deep client / SW lessons live in chat; this file is the
**hardware finish line**.

Every abbreviation is expanded the first time it appears.

---

## 0. Abbreviations (quick index)

| Abbr | Full name |
|------|-----------|
| **SoC** | System on Chip (i.MX8MP) |
| **EVK** | Evaluation Kit |
| **A53** | ARM Cortex-A53 (Linux) |
| **M7** | ARM Cortex-M7 (FreeRTOS crypto service) |
| **FreeRTOS** | Free Real-Time Operating System |
| **RPMsg** | Remote Processor Messaging |
| **NS** | Name Service (channel announce) |
| **EPT** | Endpoint (address 30 → `/dev/ttyRPMSG30`) |
| **TCM** | Tightly Coupled Memory (M7 code/data) |
| **DDR** | Double Data Rate DRAM |
| **MPU** | Memory Protection Unit |
| **RDC** | Resource Domain Controller |
| **CCM** | Clock Controller Module |
| **UART** | Universal Asynchronous Receiver/Transmitter |
| **CAAM** | Cryptographic Acceleration and Assurance Module |
| **JR** | Job Ring (CAAM command queue) |
| **HAB** | High Assurance Boot |
| **ATF** | Arm Trusted Firmware (secure world / EL3) |
| **DTB / DT** | Device Tree Blob / Device Tree |
| **OTPMK** | One-Time Programmable Master Key |
| **JDKEK** | Job Descriptor Key Encryption Key (CAAM blob wrap) |
| **AES-GCM** | Advanced Encryption Standard – Galois/Counter Mode |
| **HMAC** | Hash-based Message Authentication Code |
| **KAT** | Known Answer Test |
| **MMIO** | Memory-Mapped I/O |
| **DMA** | Direct Memory Access |
| **MID** | Master ID (AIPSTZ interconnect ID; M7 = 6) |
| **DID** | Domain ID (RDC domain; M7 = 1) |
| **M7CR** | M7 Crypto wire protocol magic |

---

## 1. What “finished” means

Default build (`armgcc/config.cmake`):

```text
M7_USE_CAAM=1    → crypto_service calls caam_* for GCM / HMAC / wrap
M7_CAAM_HW=1     → real JR1 descriptors (not scaffold stubs)
M7_CAAM_TOUCH=1  → first PING may touch CAAM MMIO and run KATs
```

Proof on the board:

```bash
./m7_crypto_client /dev/ttyRPMSG30 ping
# payload=OK-CAAM
```

That string means: clocks on, JR1 probe OK, rings programmed, ECB + GCM +
HMAC + BLOB KATs passed. Hardware CAAM is working.

---

## 2. Boot before `main()` (ownership, not C)

Order matters. Reverse it and you get no TTY or a stuck M7.

```text
U-Boot
  fatload  imx8mp_m7_caam_security.bin  →  TCM (via staging DDR)
  bootaux  M7                              ← M7 runs main() here
  fatload  Image + imx8mp-evk-rpmsg.dtb
  booti                                    ← Linux starts

Linux
  remoteproc attaches to already-running M7
  virtio RPMsg host up
  M7 NS announce "rpmsg-virtual-tty-channel-1" @ EPT 30
  modprobe rpmsg_tty  →  /dev/ttyRPMSG30
```

**Firmware outside this repo that must already be correct:**

| Piece | Why |
|-------|-----|
| ATF patches (`linux/atf/`) | Lock **JR1MID = LDID\|6**, **JRSTART** bit for JR1. M7 cannot do this (CAAM page 0 reads as zero from DID1). |
| U-Boot `mcu_rdc` (`linux/u-boot/`) | Grant CAAM peripheral to M7 domain (DID1). |
| Linux DT (`linux/imx8mp-disable-caam.dtsi`) | Disable `&crypto` / job rings so Linux does not bind CAAM. |

Job-ring map on i.MX8MP:

| Ring | Address | Owner |
|------|---------|--------|
| JR0 | `0x30901000` | HAB — do not use |
| **JR1** | `0x30902000` | **M7** (this project) |
| JR2 | `0x30903000` | OP-TEE |

Descriptors and I/O live in reserved DDR at **`0x80080000`** (TCM is not
CAAM-visible).

---

## 3. `main()` — board bring-up then scheduler

File: [`main.c`](../main.c)

```c
int main(void)
{
    BOARD_InitMemory();       /* MPU regions: TCM, DDR, device, caches */
    BOARD_RdcInit();          /* M7 → RDC domain 1; CAAM RW for DID0+DID1 */
    BOARD_InitBootPins();     /* pin mux (UART4 debug, …) */
    BOARD_InitBootClocks();   /* route/ungate clocks (CCM) — see note below */
    BOARD_InitDebugConsole(); /* PRINTF on UART4 */
    copyResourceTable();      /* publish RPMsg resource table for Linux */

    app_create_task();        /* create FreeRTOS task "CRYPTO" */
    vTaskStartScheduler();    /* never returns on success */

    /* hang if scheduler fails */
}
```

### 3.1 Clocks (one idea)

`BOARD_InitBootClocks` does **not** invent the crystal. ROM / U-Boot / A53
already run the big **PLL**s (Phase-Locked Loops). This function:

1. Points the M7 root at SysPLL1 (~800 MHz).
2. Sets UART / AHB roots.
3. **Enables clock gates** (`CLOCK_EnableClock`) so blocks can hear the tree.

Later, CAAM also enables IP-mux / sec-debug clocks in `caam8_clocks_on()`.

### 3.2 RDC and CAAM

In [`board.c`](../board.c), `BOARD_RdcInit()` assigns the M7 master to domain 1
and sets **CAAM** peripheral access for domains 0 and 1. Without that (and the
U-Boot `mcu_rdc` patch), JR1 MMIO from M7 reads back as zero.

### 3.3 Resource table

`copyResourceTable()` copies the virtio / vring description into shared memory
so Linux can attach RPMsg to an M7 that was started early from U-Boot.

### 3.4 FreeRTOS task

`app_create_task()` → `xTaskCreate(app_task, "CRYPTO", …)`.  
After `vTaskStartScheduler()`, **`app_task` is the real program**.

---

## 4. `app_task` — RPMsg first, then crypto

Still [`main.c`](../main.c). Important design choice in the finished tree:

> Bring up RPMsg **before** `crypto_service_init()`, so `/dev/ttyRPMSG*` still
> appears even if CAAM init later fails or hangs.

### 4.1 Line-by-line flow

| Step | Code | Meaning |
|------|------|---------|
| 1 | `rpmsg_lite_remote_init(SHMEM, LINK_ID, …)` | M7 is RPMsg **remote**; Linux is master. Shared base from `app.h` (`VDEV0_VRING_BASE` = `0x55000000`). |
| 2 | `rpmsg_lite_wait_for_link_up(…, RL_BLOCK)` | Wait until Linux virtio link is up. |
| 3 | `rpmsg_queue_create` + `rpmsg_lite_create_ept(…, 30, …)` | Endpoint **30** → Linux `ttyRPMSG30`. |
| 4 | `rpmsg_ns_bind` + `rpmsg_ns_announce("rpmsg-virtual-tty-channel-1")` | Name Service so `rpmsg_tty` creates the device node. |
| 5 | `crypto_service_init()` | Clear key slots; tag engine `WAIT` (CAAM) or `OK-SW`. **Does not** fully bring CAAM up yet. |
| 6 | `for (;;)` loop | `rpmsg_queue_recv` → `crypto_service_handle` → `rpmsg_lite_send`. |

```text
Linux write(/dev/ttyRPMSG30)
        ↓  virtio / shared vrings
M7  rpmsg_queue_recv(s_rx)
        ↓
    crypto_service_handle(s_rx → s_tx)
        ↓
    rpmsg_lite_send(s_tx)
        ↓
Linux read() → status + payload
```

`app_nameservice_isr_cb` is empty on purpose: M7 only **announces**; it does
not need Linux NS callbacks.

---

## 5. Protocol dictionary (fixed contract)

File: [`m7_crypto_protocol.h`](../m7_crypto_protocol.h)

Every message is **16-byte header + payload**:

| Offset | Request | Response |
|--------|---------|----------|
| 0 | magic `M7CR` | magic |
| 4 | version `1` | version |
| 6 | **cmd** | **status** |
| 8 | req_id | req_id (echo) |
| 12 | length | length |

Commands used on the path to CAAM:

| cmd | Name | Role for bring-up |
|-----|------|-------------------|
| 10 | `PING` | First call runs CAAM KATs; payload `OK-CAAM` / `FAIL-*` / `OK-SW` |
| 1 / 4 / 5 | store / encrypt / decrypt | Prove GCM on JR1 after ping |
| 1→3→2 | store / export / load blob | Prove CAAM BLOB (v2) |

Blob header versions:

| Version | Engine |
|---------|--------|
| 1 | Software wrap (`M7_USE_CAAM=0`) |
| **2** | **CAAM BLOB** (default finished firmware) |

---

## 6. `crypto_service` — dispatcher + engine switch

File: [`crypto_service.c`](../crypto_service.c)

### 6.1 Compile-time engine

```c
#if M7_USE_CAAM
  ENG_*  →  caam_aes128_gcm_* / caam_hmac_sha256
  wrap   →  caam_black_blob_*  (blob version 2)
#else
  ENG_*  →  sw_*
  wrap   →  software AES-CTR-ish wrap + HMAC (blob version 1)
#endif
```

Same `switch (hdr->cmd)` either way. That is the whole point of Step 02.

### 6.2 `crypto_service_init`

- Zero AES / HMAC key slots and validity flags.
- Set `s_engine_tag = "WAIT"` (CAAM) or `"OK-SW"`.
- **No** JR programming here — deferred to first ping / first crypto that needs CAAM.

### 6.3 `ensure_caam` (CAAM builds)

```c
if (caam_crypto_is_ready()) return 0;
s_engine_tag = caam_crypto_ping_step();  /* may take a while: KATs */
return caam_crypto_is_ready() ? 0 : -1;
```

Called from wrap/unwrap and from crypto commands that need the engine.
`PING` calls `caam_crypto_ping_step()` directly and returns the tag string.

### 6.4 `PING` case (the hardware gate)

Conceptually:

```text
if CAAM build:
    tag = caam_crypto_ping_step()   // SEE section 7
    reply payload = tag             // "OK-CAAM" or "FAIL-…"
else:
    reply payload = "OK-SW"
```

After success, later encrypt/decrypt use `ENG_AES_GCM_*` which hit JR1.

### 6.5 Encrypt path (after CAAM ready)

```text
validate header
require s_aes_valid
parse iv | aad_len | pt_len | aad | pt   (byte offsets, not a C struct)
ENG_AES_GCM_ENCRYPT(...)
make_rsp(OK, ct || tag)
```

Software vs CAAM differs only inside `ENG_*`.

---

## 7. Hardware CAAM bring-up — first `PING`

File: [`caam_crypto.c`](../caam_crypto.c) → `caam_crypto_ping_step()`

This is the **line-by-line heart** of “until HW CAAM works.”

```text
already ready?  →  return "OK-CAAM"

1. caam8_clocks_on()
2. caam8_probe_readonly()     → tag "SEE:…" or "FAIL-P:…"
3. caam8_jr_setup_rings()     → FAIL-IRSA if input ring slots == 0
4. caam8_selftest_ecb()       → NIST AES-128-ECB one block
5. caam8_jr_recycle()
6. caam8_selftest_gcm()       → NIST GCM TC2
7. caam8_jr_recycle()
8. caam8_selftest_hmac()      → RFC 4231 HMAC-SHA256 TC1
9. caam8_jr_recycle()
10. caam_blob_selftest()      → wrap then unwrap 16-byte pattern
11. s_ready = 1; return "OK-CAAM"
```

Any failure returns a short tag (`FAIL-KAT`, `FAIL-GCM`, `FAIL-HMAC`,
`FAIL-BLOB`, `FAIL-IRSA`, …) as the PING payload so boards without a usable
M7 UART still show status on Linux.

---

## 8. Job-ring driver — how a descriptor runs

Files: [`caam_imx8mp_jr.c`](../caam_imx8mp_jr.c), [`caam_imx8mp_regs.h`](../caam_imx8mp_regs.h)

### 8.1 Addresses

```text
CAAM controller   0x30900000   (page 0 — mostly invisible from M7)
JR1               0x30902000   ← CAAM8_JR_BASE
DMA arena         0x80080000   inpring + outring + desc + scratch
```

Register helpers:

```c
caam8_rd(base, off)  /  caam8_wr(base, off, val)
```

### 8.2 `caam8_clocks_on`

Enable IP-mux and sec-debug clocks via CCM, then data barriers (`DSB`/`DMB`).

### 8.3 `caam8_probe_readonly`

Write `0x80080000` to JR1 **IRBAR_LO** (input ring base address low) and read
it back. If the value sticks, M7 owns JR1 (MID 6). If everything reads zero,
ATF / RDC ownership is wrong — stop here (`FAIL-P:…`).

### 8.4 `caam8_jr_setup_rings`

1. Point `s_arena` at `0x80080000`; zero it.
2. Program IRBAR / ORBAR (input / output ring bases) to arena rings.
3. Set ring sizes to depth **1** (safe when pointer-size / stride is ambiguous).
4. Mask JR interrupts (poll mode).
5. Drain stale output entries with ORJR.
6. Clean D-cache so CAAM sees DDR.
7. Check **IRSA** (input ring slots available) ≠ 0.

**Never JRCR-reset JR1 from M7** — that clears ATF’s JRSTART with no recovery.

### 8.5 `caam8_jr0_run` (API name keeps `jr0_*`; hardware is JR1)

```text
clean D-cache
inpring[0] = descriptor address
write IRJA = 1          ← kick one job
poll ORSF until done    ← output ring slots filled
invalidate D-cache
read status from outring
write ORJR = 1          ← release output slot
return 0 if status == 0
```

Timeouts are short so a stuck CAAM does not freeze the RPMsg ping forever.

### 8.6 Descriptors

Built in DDR (`caam8_dma_desc()`). Command words from
`caam_imx8mp_regs.h` (U-Boot / MCUX patterns), for example:

| Macro | Role |
|-------|------|
| `CAAM8_CMD_JOB_HDR` | Job header + length |
| `CAAM8_CMD_KEY_C1` | Load AES key class 1 |
| `CAAM8_CMD_OP_AES_GCM_FIN` | AES-GCM encrypt finalize |
| `CAAM8_CMD_OP_HMAC_SHA256` | HMAC-SHA256 |
| `CAAM8_CMD_OP_BLOB_ENCAP/DECAP` | CAAM BLOB wrap/unwrap |

Pointers in the descriptor must be **DDR** addresses inside the scratch arena,
not TCM stack pointers.

---

## 9. Black blobs (CAAM BLOB protocol)

File: [`caam_blob.c`](../caam_blob.c)

CAAM official name: **red blob** (plaintext in, blob out, general memory).
This project calls it **black blob** because the wrap key is **OTPMK/JDKEK**,
not the Step-01 software constant.

### 9.1 Descriptor shape

```text
KEY class-2  (16-byte key modifier)
SEQ IN       (plaintext key  or  blob)
SEQ OUT      (blob           or  plaintext key)
OPERATION    ENCAP | BLOB   or   DECAP | BLOB
```

### 9.2 On the wire (blob header v2)

```text
m7cr_blob_hdr_t (28 bytes)
  magic BLOB | version 2 | type | key_len | reserved | wrap_iv[16]
payload
  CAAM blob = key_len + 48   (32-byte key blob + 16-byte MAC)
```

`wrap_iv` is the **key modifier** (includes a counter). Same modifier required
to unwrap. Blob will **not** unwrap on another chip or on `M7_USE_CAAM=0`
firmware.

`caam_blob_selftest()` on first ping: wrap a fixed 16-byte key, unwrap, compare.

---

## 10. End-to-end timeline (one mental movie)

```text
t0  U-Boot bootaux          M7 enters main()
t1  BOARD_* + FreeRTOS      clocks, RDC, MPU, task
t2  app_task RPMsg init     wait link, announce channel
t3  crypto_service_init     keys empty, tag WAIT
t4  Linux modprobe rpmsg_tty   /dev/ttyRPMSG30
t5  Client: ping
      → M7 PING
      → caam_crypto_ping_step
           clocks → probe JR1 → setup rings
           ECB KAT → GCM KAT → HMAC KAT → BLOB KAT
      → payload "OK-CAAM", s_ready=1
t6  Client: store-aes
      → key in M7 RAM + CAAM blob v2 returned
t7  Client: encrypt-gcm
      → caam_aes128_gcm_encrypt via JR1 descriptors
t8  Hardware CAAM path is the live engine
```

---

## 11. File map for this path

```text
main.c                      main → app_task RPMsg loop
board.c / clock_config.c    MPU, RDC (incl. CAAM), clocks, UART
rsc_table.*                 RPMsg resource table for Linux
m7_crypto_protocol.h        M7CR + blob header v1/v2
crypto_service.c            parse → ensure_caam → ENG_* / wrap
caam_crypto.c               ping_step KATs + GCM/HMAC API
caam_imx8mp_jr.c            JR1 poll driver + DDR arena
caam_imx8mp_regs.h          bases, JR offsets, descriptor opcodes
caam_blob.c                 OTPMK/JDKEK BLOB encap/decap
sw_crypto.c                 Step 01 engine (M7_USE_CAAM=0)
armgcc/config.cmake         M7_USE_CAAM / M7_CAAM_HW / M7_CAAM_TOUCH
linux/atf/                  JR1 MID=6 + JRSTART
linux/u-boot/               mcu_rdc CAAM → DID1
linux/imx8mp-disable-caam.dtsi   Linux must not own CAAM
crypto_rpmsg/linux/         host client (menu + CLI)
```

---

## 12. What to read next (order)

1. This file §3–§4 with [`main.c`](../main.c) open.  
2. [`crypto_service.c`](../crypto_service.c) `PING` + `ensure_caam` + `ENCRYPT_GCM`.  
3. [`caam_crypto_ping_step`](../caam_crypto.c) top to bottom.  
4. [`caam8_jr_setup_rings` / `caam8_jr0_run`](../caam_imx8mp_jr.c).  
5. [`caam_blob.c`](../caam_blob.c) `blob_job`.  
6. ATF / U-Boot / DT notes under [`linux/`](../linux/).  
7. Re-run on EVK: `ping` → `OK-CAAM` → store → encrypt → decrypt → blob export/load.

---

## 13. Honest limits (finished Step 02)

- M7 cannot start or reassign JR1; ATF must.  
- Do not JRCR-reset JR1 from M7.  
- Keys still sit in M7 RAM after unwrap (not kept as in-register CAAM black keys).  
- Blob v2 is device-bound; Step-01 v1 soft blobs will not load.  
- RPMsg payload still ~400 bytes useful plaintext + AAD.  
- Step 03 (hardening: IV policy, key lifecycle, demos) is still planned.

---

**Interview line:** *I shipped an M7 crypto service over RPMsg with a frozen
protocol, then moved AES-GCM / HMAC / key wrap from software to CAAM JR1
without rewriting the Linux client — proven by `ping` → `OK-CAAM`.*
