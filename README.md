# Local AI Assistant Daemon (C Scaffold)

This repository is now a **local-only C daemon scaffold** for Raspberry Pi voice interaction.

## Status
- Active runtime direction: `assistantd` (C).
- Phase: scaffold only (no full STT/LLM/TTS runtime implementation yet).

## Runtime Entry Point
- `assistantd --config /etc/local-ai-assistant/assistantd.env`
- `assistantd --foreground`

## How to install on Pi?
- Install locally on Pi:
```bash
scripts/install_assistantd.sh --local-install
```

- Install remotely over SSH from your laptop:
```bash
scripts/install_assistantd.sh --ssh shiori@100.65.234.65
```

## Local-Only Policy
- Remote mode is intentionally unsupported in this phase.
- Config must set `ASSISTANT_MODE=local`.

## Project Layout
- `include/assistantd/`: public interfaces for daemon modules.
- `src/assistantd/`: scaffold module implementations + TODO playbooks.
- `tests/c/`: C test skeletons.
- `config/assistantd.env.example`: daemon config template.
- `systemd/assistantd.service`: service scaffold.
- `scripts/install_assistantd.sh`: installation scaffold.
- `HUMANS.md`: detailed human-facing operator/developer guide.

## Build And Test
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Service Scaffold
Service file exists at `systemd/assistantd.service` and is intended to run:
```bash
/usr/local/bin/assistantd --config /etc/local-ai-assistant/assistantd.env
```