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
| 1 | STORE_AES_KEY | 16-byte AES key | soft blob |
| 2 | LOAD_AES_BLOB | soft blob | empty |
| 3 | EXPORT_AES_BLOB | empty | soft blob |
| 4 | ENCRYPT_GCM | `iv[12] \| aad_len:u16 \| pt_len:u16 \| aad \| pt` | `ct \| tag[16]` |
| 5 | DECRYPT_GCM | `iv[12] \| aad_len:u16 \| ct_len:u16 \| aad \| ct \| tag[16]` | `pt` |
| 6 | STORE_HMAC_KEY | `key_len:u16 \| key` | soft blob |
| 7 | LOAD_HMAC_BLOB | soft blob | empty |
| 8 | EXPORT_HMAC_BLOB | empty | soft blob |
| 9 | SIGN_HMAC | data | `mac[32]` |
| 10 | PING | empty | `OK-SW` / `OK-CAAM` / `FAIL-*` (engine status; no M7 UART needed) |

## Status codes

0 OK, 1 bad magic, 2 bad version, 3 bad cmd, 4 bad length, 5 no key,
6 crypto fail, 7 auth fail, 8 blob fail, 9 busy, 10 internal.

## Soft blob

`magic('BLOB') | version | type | key_len | reserved | wrap_iv[16] | wrapped_key | hmac[32]`

Type 1 = AES-128, type 2 = HMAC key. Wrapped with a device soft wrap key via AES-CTR + HMAC-SHA256 (prototype only).
