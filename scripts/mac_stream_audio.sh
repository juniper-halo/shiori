#!/usr/bin/env bash
set -euo pipefail

HOST="127.0.0.1"
PORT="5555"
INPUT=":0"
ONCE="false"

log() {
  echo "[mac_stream_audio] $*"
}

usage() {
  cat <<'EOF'
Stream microphone PCM from macOS to assistantd network capture mode.

Usage:
  scripts/mac_stream_audio.sh [options]

Options:
  --host <host>         Destination host (default: 127.0.0.1).
  --port <port>         Destination TCP port (default: 5555).
  --input <spec>        ffmpeg avfoundation input (default: :0).
  --once                Exit after one stream session.
  --list-devices        List macOS avfoundation devices and exit.
  --help                Show help.

Examples:
  # In one terminal, tunnel Mac localhost:5555 to Pi localhost:5555.
  ssh -N -L 5555:127.0.0.1:5555 pi@raspberrypi.local

  # In another terminal, stream default microphone over the tunnel.
  scripts/mac_stream_audio.sh --host 127.0.0.1 --port 5555
EOF
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

list_devices() {
  require_cmd ffmpeg
  ffmpeg -f avfoundation -list_devices true -i "" 2>&1 || true
}

stream_once() {
  ffmpeg \
    -hide_banner \
    -loglevel warning \
    -f avfoundation \
    -i "${INPUT}" \
    -ac 1 \
    -ar 16000 \
    -f s16le \
    - \
    | nc "${HOST}" "${PORT}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host)
      HOST="${2:-}"
      shift 2
      ;;
    --port)
      PORT="${2:-}"
      shift 2
      ;;
    --input)
      INPUT="${2:-}"
      shift 2
      ;;
    --once)
      ONCE="true"
      shift
      ;;
    --list-devices)
      list_devices
      exit 0
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

require_cmd ffmpeg
require_cmd nc

if [[ "${ONCE}" == "true" ]]; then
  log "starting single stream session to ${HOST}:${PORT} input=${INPUT}"
  stream_once
  exit 0
fi

log "streaming microphone to ${HOST}:${PORT} input=${INPUT}"
log "press Ctrl+C to stop"
while true; do
  if ! stream_once; then
    log "stream interrupted; retrying in 1s"
    sleep 1
  fi
done
