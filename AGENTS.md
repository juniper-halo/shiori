# AGENTS.md

## Purpose
This repo contains a local-only C daemon scaffold (`assistantd`) for orchestrating:
- audio capture,
- VAD segmentation,
- STT,
- local LLM inference adapter,
- TTS,
- playback.

## Current Reality
- Runtime implementation is intentionally incomplete.
- Module files include Doxygen TODO playbooks that define implementation contracts.
- Archived Python runtime is in `archive/python-runtime/`.

## Canonical Commands
- Configure build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- Build: `cmake --build build`
- Test: `ctest --test-dir build --output-on-failure`
- Run scaffold daemon: `./build/assistantd --config ./config/assistantd.env.example --foreground`

## Local-Only Contract
- `ASSISTANT_MODE` must be `local`.
- No remote mode or fallback path should be introduced in this phase.

## Coding Rules For This Scaffold Phase
- Keep interfaces stable and implementation minimal.
- Add/maintain Doxygen TODO playbook blocks for module contracts.
- Preserve fail-fast status returns for unimplemented runtime paths.
- Prefer explicit status enums over implicit boolean failures.

## Test Expectations
- Build and C tests must pass.
- C tests are shape/contract checks, not full runtime validation.
- CI must run CMake configure/build/ctest and TODO doc-lint.

## Done Criteria
- New module scaffolds compile.
- Public interfaces are documented through TODO playbooks.
- Local-only policy remains enforced in config validation.
