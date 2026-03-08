# C Daemon Scaffold Notes

This repository now targets a local-only C daemon (`assistantd`) as the active runtime direction.

## Current Phase
- Compile-ready scaffold only.
- No full STT/LLM/TTS runtime implementation yet.
- Module-level Doxygen TODO blocks are the implementation playbook.

## Local-Only Policy
- `ASSISTANT_MODE` must be `local`.
- Remote fallback/remote mode are intentionally unsupported in this phase.

## Runtime Scope
- This repository currently keeps only the C daemon scaffold and related docs/tests.
