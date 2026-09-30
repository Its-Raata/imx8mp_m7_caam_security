# Roadmap

This repository is organized as **numbered steps**. Each step is a resume-visible
milestone: what works, what files matter, and what comes next.

| Step | Title | Git tag (when frozen) | Status |
|------|--------|----------------------|--------|
| 01 | Software crypto + RPMsg + Linux client | `step-01` | Done |
| 02 | CAAM engine + black blobs | `step-02` | Design / next |
| 03 | Hardening & demos | `step-03` | Planned |

## Design rule (important)

**Do not break the wire protocol** between steps.

- Linux client and `m7_crypto_protocol.h` stay the contract.
- Step 2 replaces *how* AES-GCM / HMAC / blobs are computed (CAAM), not *what*
  commands look like on `/dev/ttyRPMSG30`.

That shows systems judgment: stable interfaces, swappable engines.

## How to present this on GitHub

1. **README** — status table + architecture (already the landing page).
2. **`docs/step-XX-*.md`** — one page per milestone (goal, done criteria, limits).
3. **Tags** — after each milestone: `git tag -a step-01 -m "..." && git push --tags`.
4. **Optional branches** — `step/01-software-crypto` kept for browsing old tree;
   `main` always advances.
5. **Releases** — GitHub Release notes that copy the step doc summary.

## Learning path (for the author)

Documented while building: boot/MPU/RDC → FreeRTOS/RPMsg → protocol → soft blob
→ GCM IV/AAD/tag → CAAM ownership.

Interview line: *“I shipped an M7 crypto service over RPMsg with a frozen
protocol, then moved the engine from software AES to CAAM without changing the
Linux client.”*
