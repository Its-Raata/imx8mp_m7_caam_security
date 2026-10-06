# M7CR wire protocol

**Author:** Raata <its.raata@gmail.com>

Little-endian. One request and one response per message (or ttyRPMSG frame).

## Header (16 bytes)

Request:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | magic `0x4D374352` (`M7CR`) |
| 4 | 2 | version `1` |
| 6 | 2 | cmd |
| 8 | 4 | req_id |
| 12 | 4 | length (payload bytes after header) |

Response: same layout, but `cmd` field is replaced by `status`.

## Commands

| cmd | Name | Request payload | Response payload |
|-----|------|-----------------|------------------|
| 1 | STORE_AES_KEY | 16-byte AES key | blob |
| 2 | LOAD_AES_BLOB | blob | empty |
| 3 | EXPORT_AES_BLOB | empty | blob |
| 4 | ENCRYPT_GCM | `iv[12] \| aad_len:u16 \| pt_len:u16 \| aad \| pt` | `ct \| tag[16]` |
| 5 | DECRYPT_GCM | `iv[12] \| aad_len:u16 \| ct_len:u16 \| aad \| ct \| tag[16]` | `pt` |
| 6 | STORE_HMAC_KEY | `key_len:u16 \| key` | blob |
| 7 | LOAD_HMAC_BLOB | blob | empty |
| 8 | EXPORT_HMAC_BLOB | empty | blob |
| 9 | SIGN_HMAC | data | `mac[32]` |
| 10 | PING | empty | `OK-SW` / `OK-CAAM` / `FAIL-*` (engine status; no M7 UART needed) |

## Status codes

0 OK, 1 bad magic, 2 bad version, 3 bad cmd, 4 bad length, 5 no key,
6 crypto fail, 7 auth fail, 8 blob fail, 9 busy, 10 internal.

## Blob header (28 bytes) + payload

`magic('BLOB') | version | type | key_len | reserved | wrap_iv[16] | payload`

Type 1 = AES-128, type 2 = HMAC key.

| Version | Engine | `wrap_iv` | Payload |
|---------|--------|-----------|---------|
| 1 | `M7_USE_CAAM=0` | AES-CBC IV | padded CBC ciphertext + HMAC-SHA256 (Step 01 software wrap) |
| 2 | CAAM (default) | 16-byte key modifier | CAAM BLOB (`key_len + 48`: 32-byte key blob + 16-byte MAC) |

Version 2 is bound to this chip (OTPMK / JDKEK). Another board cannot unwrap it.
Step-01 v1 files will not load on CAAM firmware.
