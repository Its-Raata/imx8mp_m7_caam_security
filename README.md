# i.MX8MP Cortex-M7 crypto service

Raata — first firmware.

Linux on Cortex-A53 sends encrypt, decrypt, and HMAC requests to Cortex-M7.
M7 keeps the keys and returns ciphertext or a MAC. Crypto is software today
(AES-128-GCM and HMAC-SHA256). A later step can swap that path for CAAM
without changing the wire format.

## Architecture

```text
Linux userspace                         Cortex-M7 (FreeRTOS)
m7_crypto_client                        main.c
        |                                 RPMsg-Lite loop
        |  raw write / read               endpoint 30
        v                                 v
/dev/ttyRPMSG30  <--- virtio RPMsg --->  rpmsg-virtual-tty-channel-1
        |                                 |
        |         shared vring            v
        +-------- 0x55000000 -------->  crypto_service.c
                                          keys + soft blob
                                          v
                                        sw_crypto.c
                                          AES-GCM, SHA-256, HMAC
```

| Piece | File | Job |
|-------|------|-----|
| Wire format | `m7_crypto_protocol.h` | Little-endian request and response. One message, one reply. |
| M7 task | `main.c` | Start the board, announce the RPMsg channel, pass each message to the service. |
| Commands | `crypto_service.c` | Store, export, and load keys. Encrypt, decrypt, sign. |
| Algorithms | `sw_crypto.c` | AES-128-GCM, SHA-256, HMAC-SHA256, AES-CBC wrap. |
| Linux client | `crypto_rpmsg/linux/m7_crypto_client.c` | Opens `/dev/ttyRPMSG30` in raw mode and prints the result. |
| Protocol tables | `crypto_rpmsg/doc/PROTOCOL.md` | Command bytes and payload layout. |

`board.c`, `pin_mux.c`, `clock_config.c`, and `rsc_table.c` are the NXP EVK
startup used by the RPMsg examples: MPU, RDC domain 1, clocks, UART4 console,
and the virtio resource table.

Keys stay in M7 RAM. A soft blob is AES-128-CBC plus HMAC so Linux can save
a key and load it again. The wrap key in `crypto_service.c` is a prototype
constant, not a CAAM black key.

## Boot

This image is started from U-Boot before Linux, not from `remoteproc`.
Linux uses `imx8mp-evk-rpmsg.dtb` so the vring at `0x55000000` stays reserved.

Copy `armgcc/debug/imx8mp_m7_caam_security.bin` to the FAT boot partition, then:

```text
setenv m7load 'fatload mmc 2:1 0x80000000 imx8mp_m7_caam_security.bin'
setenv m7copy 'cp.b 0x80000000 0x7e0000 0x20000'
setenv m7aux  'bootaux 0x7e0000'
setenv loadk  'fatload mmc 2:1 0x40480000 Image'
setenv loadd  'fatload mmc 2:1 0x43000000 imx8mp-evk-rpmsg.dtb'
setenv bootargs 'console=ttymxc1,115200 root=/dev/mmcblk2p2 rootwait rw clk_ignore_unused'
setenv bootcmd 'run m7load; run m7copy; run m7aux; run loadk; run loadd; booti 0x40480000 - 0x43000000'
saveenv
reset
```

`0x7e0000` is the Cortex-M7 TCM alias of load address `0x0`.

## Linux test

```bash
modprobe imx_rpmsg_tty
gcc -O2 -o m7_crypto_client m7_crypto_client.c
./m7_crypto_client /dev/ttyRPMSG30 ping
./m7_crypto_client /dev/ttyRPMSG30 store-aes 00112233445566778899aabbccddeeff
./m7_crypto_client /dev/ttyRPMSG30 encrypt-gcm 000000000000000000000000 "" "hello"
./m7_crypto_client /dev/ttyRPMSG30 decrypt-gcm 000000000000000000000000 "" "<ct+tag hex>"
```

`status=0 payload=OK` on ping means the channel and protocol are working.

## Build

MCUXpresso SDK for MIMX8ML8, CMake preset `debug` in `armgcc/`.
Set `SdkRootDirPath` to your SDK root before configure:

```bash
export SdkRootDirPath=/path/to/mcuxsdk-aarch64
# optional local edit of armgcc/mcux_include.json "SdkRootDirPath"
cmake --preset debug -S armgcc
cmake --build armgcc/debug
```

Output: `armgcc/debug/imx8mp_m7_caam_security.bin`

Plaintext plus AAD must stay under about 400 bytes. One RPMsg buffer is 496 bytes.
