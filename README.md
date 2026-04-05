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
- `scripts/mac_stream_audio.sh`: macOS microphone sender for dev `network_tcp` capture mode.
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

## Dev Network Capture (Mac -> Pi)
Use this development-only workflow to run capture -> VAD -> STT on the Pi while sending microphone audio from a Mac.

1. Configure Pi daemon env:
```bash
AUDIO_INPUT_MODE=network_tcp
AUDIO_NETWORK_PORT=5555
DEV_PIPELINE_MODE=stt_only
```

2. From the Mac, create an SSH tunnel to Pi loopback:
```bash
ssh -N -L 5555:127.0.0.1:5555 pi@<pi-host>
```

3. From the Mac, stream microphone PCM into the tunnel:
```bash
scripts/mac_stream_audio.sh --host 127.0.0.1 --port 5555
```

4. On the Pi, run:
```bash
./build/assistantd --config ./config/assistantd.env.example --foreground
```

In `stt_only` mode, finalized utterances are transcribed and logged continuously, and LLM/TTS modules are not initialized.
