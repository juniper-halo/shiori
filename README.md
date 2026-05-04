# Contributors
Juno: STT + integration of individual components (e.g. STT, VAD, LLM etc.)
Jack: LLM pipeline
Alex: STT pipeline (not implemented / integrated)

## Baymax

`Baymax` is a local-only voice assistant daemon written in C. It is intended
to run an always-listening speech pipeline on a local machine, using local audio
devices and local model servers instead of cloud services.

The current implementation is a working scaffold for the core runtime:

1. Capture raw microphone audio from ALSA through `arecord`.
2. Buffer audio in a single-producer/single-consumer ring buffer.
3. Segment speech with WebRTC VAD.
4. Persist completed utterances as WAV files.
5. Send utterance WAV files to a local `whisper.cpp` server for STT.
6. Send transcripts to a local `llama-server` OpenAI-compatible endpoint.
7. Queue LLM responses for the future TTS/playback stage.

The project is designed for a local assistant persona. The checked-in system
prompt in `config/system_prompt.txt` describes a spoken, friendly "Baymax"
style companion.

## Repository Layout

- `src/assistantd/` contains the daemon implementation.
- `include/assistantd/` contains public module headers.
- `config/assistantd.env` contains a sample local runtime configuration.
- `config/system_prompt.txt` contains the LLM system prompt.
- `tests/c/` contains C shape and behavior tests for the implemented modules.
- `src/third_party/` vendors small dependencies used by the daemon.

## Major Components

- `main.c` parses `--config`, installs shutdown handlers, starts the supervisor,
  and runs the foreground loop.
- `config.c` loads key/value configuration, applies defaults, and validates
  local-only runtime settings.
- `audio_capture.c` manages an `arecord` child process and reads S16_LE mono
  PCM from stdout.
- `ring_buffer.c` implements a C11 atomic SPSC ring buffer for PCM frames.
- `vad_detector.c` wraps WebRTC VAD and emits speech start, continue, and end
  events.
- `supervisor.c` owns the runtime pipeline, routes capture frames into VAD,
  writes completed utterances to WAV files, runs STT and LLM, and queues
  responses.
- `stt_adapter.c` posts WAV files to a local `whisper.cpp` HTTP server.
- `llm_adapter.c` posts transcripts to a local `llama-server` chat completions
  endpoint using the configured system prompt.
- `tts_adapter.c` is initialized but synthesis is not implemented yet.
- `playback.c` can run `aplay` for a WAV file, but supervisor integration is
  not complete yet.

## What Works

The following pieces are implemented:

- Local config loading and validation for `ASSISTANT_MODE=local`.
- Foreground daemon startup and clean signal-driven shutdown.
- Managed ALSA capture subprocess startup, nonblocking reads, and cleanup.
- C11 atomic SPSC ring buffer with overflow and underflow counters.
- WebRTC VAD-backed speech segmentation.
- Utterance assembly and WAV artifact writing.
- STT HTTP adapter for a local `whisper.cpp` server at `/inference`.
- LLM HTTP adapter for a local OpenAI-compatible `llama-server` endpoint at
  `/v1/chat/completions`.
- LLM timeout handling and shutdown cancellation.
- Playback helper around `aplay`.
- Tests for ring buffer, config, capture shape, VAD shape, STT shape,
  supervisor shape, LLM response queue shape, and LLM adapter shape.

The supervisor currently processes this path:

```text
arecord -> ring buffer -> VAD -> utterance WAV -> STT -> LLM -> response queue
```

## What Does Not Work Yet

The assistant is not yet a complete end-to-end spoken conversation loop.

Known incomplete areas:

- TTS synthesis through Piper is still a scaffold and returns
  `ASSISTANTD_ERR_UNIMPLEMENTED`.
- The supervisor drains queued LLM responses as a placeholder instead of
  synthesizing them to audio.
- Playback exists as a helper but is not connected to queued LLM responses.
- Daemonization is scaffold-only; the binary currently logs a warning and runs
  in the foreground.
- Capture depends on `/usr/bin/arecord`, so non-Linux or non-ALSA development
  machines will stop at the capture boundary.
- Runtime model servers are expected to be managed externally.

## Runtime Dependencies

At runtime, a full local pipeline expects:

- ALSA `arecord` for capture.
- ALSA `aplay` for playback once the output stage is wired.
- A local `whisper.cpp` HTTP server for speech-to-text.
- A local `llama-server` instance exposing an OpenAI-compatible
  `/v1/chat/completions` endpoint.
- Piper and a voice model for the future TTS stage.

Build-time dependencies include:

- CMake 3.20 or newer.
- A C17 compiler.
- libcurl development headers and library.
- POSIX process APIs.

## Configuration

The daemon reads a key/value env-style config file. By default it looks for:

```text
/etc/local-ai-assistant/assistantd.env
```

For local development, pass the checked-in sample explicitly:

```sh
./build/assistantd --config config/assistantd.env --foreground
```

Important settings:

- `ASSISTANT_MODE=local` is required.
- `RUNTIME_DIR` is where utterance WAV artifacts are written temporarily.
- `AUDIO_CAPTURE_DEVICE` and `AUDIO_PLAYBACK_DEVICE` select ALSA devices.
- `STT_API_BASE_URL` points to the `whisper.cpp` server.
- `LLM_API_BASE_URL` points to the local `llama-server` `/v1` base URL.
- `LLM_MODEL` names the local model exposed by `llama-server`.
- `LLM_SYSTEM_PROMPT_PATH` points to the prompt file loaded at startup.
- `TTS_BIN` and `TTS_VOICE_PATH` are reserved for the Piper TTS stage.
- `VAD_AGGRESSIVENESS` must be from `0` to `3`.
- `VAD_SILENCE_MS` controls end-of-speech detection.

## Build

From the repository root:

```sh
cmake -S . -B build
cmake --build build
```

Run the daemon in foreground mode with the sample config:

```sh
./build/assistantd --config config/assistantd.env --foreground
```

The sample config contains machine-specific paths, especially
`LLM_SYSTEM_PROMPT_PATH`, so adjust it before running outside the original
development environment.

## Tests

When CMake testing is enabled, the build defines tests for the current module
surface. Run them with:

```sh
ctest --test-dir build --output-on-failure
```
