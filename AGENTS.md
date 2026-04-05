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

- Runtime implementation is intentionally incomplete for the full voice pipeline.
- **LLM adapter (`llm_adapter`)**: concrete implementation is in place — libcurl HTTP to a local OpenAI-compatible server, cJSON request/response, system prompt file loading, 40s timeout, shutdown cancellation via progress callback. Supervisor still does not wire a full end-to-end interaction loop (Phase 4).
- **VAD detector (`vad_detector`)**: implemented with `libfvad` frame classification and local speech state transitions (`START/CONTINUE/END`) behind the existing interface.
- Other adapters (STT, TTS, orchestration wiring) remain scaffold/TODO where noted in `HUMANS.md`.
- Module files include Doxygen `@todo` playbook blocks that define implementation contracts; keep them accurate when you change behavior.

## Build Prerequisites (LLM path)

- CMake >= 3.20, C17 compiler.
- **libcurl** with development headers so CMake can `find_package(CURL)` (e.g. macOS: Xcode SDK; Raspberry Pi OS: `libcurl4-openssl-dev`).
- **libfvad** is vendored under `src/third_party/libfvad` and built from source with `assistantd_core`.
- Vendored **cJSON** is compiled as part of `assistantd_core` (`src/third_party/cJSON/`).

## Canonical Commands

- Configure build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- Build: `cmake --build build`
- Test: `ctest --test-dir build --output-on-failure`  
  (If `test_supervisor_shape` hangs in your environment, narrow with `-R 'ring_buffer_shape|config_shape|llm_adapter_shape'`.)
- Run scaffold daemon: `./build/assistantd --config ./config/assistantd.env.example --foreground`

### Local LLM server (llama.cpp `llama-server`, not built by this repo)

Run from the repo root so `models/...` resolves:

```bash
llama-server --model models/SmolLM2-1.7B-Instruct-Q4_K_M.gguf \
  --host 127.0.0.1 --port 8080 --ctx-size 2048 --threads 4
```

Match `config/assistantd.env.example`: `LLM_API_BASE_URL=http://127.0.0.1:8080/v1`, `LLM_MODEL=SmolLM2-1.7B-Instruct-Q4_K_M`.

Quick API check (server must be running):

```bash
curl -s http://127.0.0.1:8080/v1/models | head
```

### System prompt

- Example prompt text: `config/system_prompt.txt` (Baymax-style daily companion; TTS-oriented constraints).
- Deployed path in example env: `LLM_SYSTEM_PROMPT_PATH` (see `config/assistantd.env.example`). For local dev you can point this at `./config/system_prompt.txt` via your env file.

### Model weights

- Large GGUF files live under `models/` and are **gitignored**; download separately (e.g. SmolLM2-1.7B-Instruct Q4_K_M). Do not commit binaries.

## Local-Only Contract

- `ASSISTANT_MODE` must be `local`.
- No remote mode or fallback path should be introduced in this phase.
- LLM traffic must target the configured local base URL (typically `127.0.0.1`).

## Coding Rules For This Scaffold Phase

- Keep public interfaces stable unless a design change is agreed; extend with new fields/functions deliberately.
- Add/maintain Doxygen `@todo` playbook blocks for module contracts where they exist.
- Preserve fail-fast status returns; map errors to `assistantd_status_t`.
- Prefer explicit status enums over implicit boolean failures.

## Test Expectations

- Build and C tests must pass in CI.
- Shape tests: `test_ring_buffer_shape`, `test_ring_buffer_spsc`, `test_config_shape`, `test_audio_capture_shape`, `test_vad_detector_shape`, `test_supervisor_shape`, `test_llm_adapter_shape`.
- `test_llm_adapter_shape` covers init (prompt file, curl handle), generate error paths (e.g. connection refused), shutdown, and `assistantd_llm_set_shutdown_flag`. Not a substitute for integration tests against a live llama-server.

## CI Expectations

- CMake configure must find CURL.
- Build, ctest, and TODO doc-lint as configured in `.github/workflows/ci.yml`.

## Done Criteria (ongoing)

- New and changed modules compile and tests pass.
- Public LLM API surface remains documented in `include/assistantd/llm_adapter.h` and playbook in `src/assistantd/llm_adapter.c`.
- Local-only policy remains enforced in `config.c` validation.
