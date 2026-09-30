# Step 02 — CAAM crypto engine (plan)

**Author:** Raata \<its.raata@gmail.com\>  
**Status:** Next implementation  
**Tag:** `step-02` (when code lands)  
**Goal:** Same RPMsg protocol and Linux client; crypto and blobs backed by **CAAM**.

## Why this step matters

Step 1 proved the architecture. Step 2 shows SoC security integration:

- Hardware AES-GCM / HMAC instead of pure software
- **Black blobs** (or equivalent CAAM blob APIs) instead of a hardcoded wrap key
- Clear story about **who owns CAAM job rings** (M7 vs Linux)

Interview value: multicore + crypto accelerator + stable ABI.

## Non-negotiables

1. **No protocol break** — `m7_crypto_protocol.h` and `m7_crypto_client` stay valid.
2. **One engine switch** — `crypto_service.c` calls CAAM wrappers instead of `sw_*`
   (keep `sw_crypto.c` as fallback behind a compile flag if useful).
3. **Document ownership** — Linux must not use the same job ring M7 uses.

## Approach (recommended order)

### 1. Platform prep

- Confirm CAAM is accessible from M7 in SDK (`fsl_caam` / job ring APIs).
- Device tree / SCFW / RDC: reserve **at least one job ring** for M7; stop Linux
  from claiming it (`/dev/crypto` / `caam` driver binding).
- Clocks and RDC domain already partly handled in Step 1 bring-up; extend for CAAM.

### 2. Thin CAAM adapters (new files)

Suggested names (to keep Step 1 readable in git history):

```text
caam_crypto.c / caam_crypto.h   # AES-GCM encrypt/decrypt, HMAC
caam_blob.c   / caam_blob.h     # black/red blob wrap & unwrap
```

Map 1:1 to today’s calls:

| Step 1 | Step 2 |
|--------|--------|
| `sw_aes128_gcm_encrypt` | `caam_aes128_gcm_encrypt` |
| `sw_aes128_gcm_decrypt` | `caam_aes128_gcm_decrypt` |
| `sw_hmac_sha256` | `caam_hmac_sha256` |
| `wrap_key_blob` / soft CBC | `caam_black_blob_wrap` |
| `unwrap_key_blob` | `caam_black_blob_unwrap` |

### 3. Wire into `crypto_service.c`

- `#ifdef USE_CAAM` (or CMake option) select CAAM vs software.
- Keep soft-blob **on-wire layout** only if still useful; prefer CAAM blob bytes
  as the blob payload Linux stores (document length/format in PROTOCOL.md).

### 4. Prove parity

Same client commands as Step 1:

```bash
./m7_crypto_client $DEV ping
./m7_crypto_client $DEV store-aes ...
./m7_crypto_client $DEV encrypt-gcm ...
./m7_crypto_client $DEV decrypt-gcm ...
```

Plus: export blob on M7, reboot M7 path, load blob, decrypt again.

### 5. Freeze milestone

- Update this doc status → Done  
- Tag `step-02`  
- GitHub Release notes: “CAAM engine, protocol unchanged”

## Risks (call them out publicly)

| Risk | Mitigation |
|------|------------|
| Linux and M7 fight over CAAM | Dedicated JR + DT / driver disable |
| Blob format change | Version field in blob header; document in PROTOCOL.md |
| Debug harder than software | Keep `USE_CAAM=0` software path for bring-up |

## Out of scope for Step 2

- Full key provisioning HSM story
- TLS stack
- Large multipart messages beyond RPMsg MTU

Those belong in Step 3+.

## Implementation kickoff checklist

When coding starts, open a branch `step/02-caam` and tick:

- [ ] CAAM init from M7 succeeds (one self-test encrypt)
- [ ] GCM encrypt/decrypt via CAAM matches known answer (or matches Step 1 vector)
- [ ] HMAC via CAAM
- [ ] Black blob export/import survives M7 restart (with Linux-held blob file)
- [ ] Client unchanged except docs
- [ ] README status table: Step 2 Done

## Related reading

- NXP CAAM driver headers in MCUXpresso SDK (`fsl_caam.h`)
- i.MX8MP security / job ring assignment in Linux DT and reference manual
