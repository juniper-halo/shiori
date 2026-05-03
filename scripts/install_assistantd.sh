#!/usr/bin/env bash
# Fail fast on errors, unset vars, and pipeline failures.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
UNIT_SRC="${ROOT_DIR}/systemd/assistantd.service"
CONFIG_SRC="${ROOT_DIR}/config/assistantd.env.example"
WHISPER_CPP_VERSION="v1.8.4"
WHISPER_CPP_ARCHIVE_URL="https://github.com/ggml-org/whisper.cpp/archive/refs/tags/${WHISPER_CPP_VERSION}.tar.gz"
WHISPER_MODEL_NAME="ggml-base.en-q5_1.bin"
WHISPER_MODEL_DOWNLOAD_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${WHISPER_MODEL_NAME}"
WHISPER_MODEL_INSTALL_DIR="/usr/local/share/assistantd/models"
WHISPER_MODEL_INSTALL_PATH="${WHISPER_MODEL_INSTALL_DIR}/${WHISPER_MODEL_NAME}"

# Default options; may be overridden by CLI flags.
SSH_TARGET=""
REMOTE_DIR=""
LOCAL_INSTALL=false
SKIP_PACKAGES=false
SKIP_ENABLE=false
NO_BUILD=false
SERVICE_USER="pi"
SERVICE_GROUP="pi"
SSH_TTY=true
SUDO_KEEPALIVE_PID=""
SUDO_CMD=(sudo)
SSH_CONTROL_PATH=""
SSH_CONTROL_DIR=""
SSH_MASTER_TARGET=""

# Bluetooth setup options.
SETUP_BLUETOOTH=false
BT_MAC=""
BT_STACK="bluealsa"
SKIP_BT_PAIR=false
UPDATE_BT_CONFIG=false
BT_SCAN_SECS=10
BT_CONNECT_ATTEMPTS=3

log() {
  echo "[install_assistantd] $*"
}

usage() {
  cat <<'EOF'
Usage:
  scripts/install_assistantd.sh [options]

Modes:
  1) Local install on current machine:
       scripts/install_assistantd.sh --local-install
  2) SSH deploy + remote install (run from laptop):
       scripts/install_assistantd.sh --ssh pi@raspberrypi.local

Options:
  --ssh <user@host>      Deploy over SSH to target and run installer remotely.
  --remote-dir <path>    Remote repo location (default: /home/<ssh-user>/assistantd).
  --repo-root <path>     Override repository root (internal use for remote mode).
  --local-install        Force local install path (used on remote host).
  --skip-packages        Skip apt package installation.
  --skip-enable          Install service but do not enable/start it.
  --no-build             Skip CMake build (expects build/assistantd to exist).
  --config-src <path>    Config template path (default: config/assistantd.env.example).
  --service-user <user>  systemd User= value (default: pi).
  --service-group <grp>  systemd Group= value (default: pi).
  --ssh-no-tty           Disable pseudo-terminal allocation for remote SSH run.
  --help                 Show help.

Bluetooth Setup (requires --local-install or --ssh):
  --setup-bluetooth      Install BT stack, pair device, and validate audio output.
  --bt-mac <MAC>         Bluetooth device MAC address (required with --setup-bluetooth).
                         Format: AA:BB:CC:DD:EE:FF
  --bt-stack <stack>     BT audio stack: bluealsa (default, lightweight) or pipewire.
  --skip-bt-pair         Skip pairing step; assume device already trusted, connect only.
  --update-bt-config     Write discovered device string to installed env file as AUDIO_DEVICE.
                         (After Phase 1 config split this becomes AUDIO_PLAYBACK_DEVICE.)
EOF
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

cpu_jobs() {
  getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2
}

# Install whisper-cli to /usr/local/bin when not already present.
install_whisper_cli() {
  if [[ -x /usr/local/bin/whisper-cli ]]; then
    log "whisper-cli already installed at /usr/local/bin/whisper-cli"
    return
  fi

  require_cmd curl
  require_cmd tar
  require_cmd cmake
  require_cmd mktemp

  local work_dir=""
  work_dir="$(mktemp -d "${TMPDIR:-/tmp}/assistantd-whisper-XXXXXXXX")"
  local archive_path="${work_dir}/whisper.cpp.tar.gz"
  local src_dir="${work_dir}/whisper.cpp-${WHISPER_CPP_VERSION#v}"
  local build_dir="${src_dir}/build"

  log "Installing whisper-cli from whisper.cpp ${WHISPER_CPP_VERSION}"
  curl -fsSL "${WHISPER_CPP_ARCHIVE_URL}" -o "${archive_path}"
  tar -xzf "${archive_path}" -C "${work_dir}"

  if [[ ! -d "${src_dir}" ]]; then
    src_dir="$(find "${work_dir}" -maxdepth 1 -type d -name "whisper.cpp-*" | head -n 1)"
    build_dir="${src_dir}/build"
  fi
  if [[ -z "${src_dir}" || ! -d "${src_dir}" ]]; then
    echo "Failed to locate whisper.cpp source tree in ${work_dir}" >&2
    exit 1
  fi

  cmake \
    -S "${src_dir}" \
    -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DWHISPER_BUILD_TESTS=OFF \
    -DWHISPER_BUILD_SERVER=OFF
  cmake --build "${build_dir}" --target whisper-cli -j "$(cpu_jobs)"

  if [[ ! -x "${build_dir}/bin/whisper-cli" ]]; then
    echo "whisper-cli build output missing at ${build_dir}/bin/whisper-cli" >&2
    exit 1
  fi

  "${SUDO_CMD[@]}" install -m 755 "${build_dir}/bin/whisper-cli" /usr/local/bin/whisper-cli
  rm -rf "${work_dir}"
}

# Ensure the quantized base Whisper model is available on the host.
install_whisper_model() {
  local repo_model_path="${ROOT_DIR}/src/third_party/whisper_models/${WHISPER_MODEL_NAME}"
  local tmp_model_path=""

  "${SUDO_CMD[@]}" install -d -m 755 "${WHISPER_MODEL_INSTALL_DIR}"

  if [[ -r "${WHISPER_MODEL_INSTALL_PATH}" ]]; then
    log "Whisper model already present at ${WHISPER_MODEL_INSTALL_PATH}"
    return
  fi

  if [[ -r "${repo_model_path}" ]]; then
    log "Installing whisper model from repository copy: ${repo_model_path}"
    "${SUDO_CMD[@]}" install -m 644 "${repo_model_path}" "${WHISPER_MODEL_INSTALL_PATH}"
    return
  fi

  require_cmd curl
  require_cmd mktemp

  tmp_model_path="$(mktemp "${TMPDIR:-/tmp}/assistantd-whisper-model-XXXXXXXX.bin")"
  log "Downloading whisper model: ${WHISPER_MODEL_NAME}"
  curl -fsSL "${WHISPER_MODEL_DOWNLOAD_URL}" -o "${tmp_model_path}"
  "${SUDO_CMD[@]}" install -m 644 "${tmp_model_path}" "${WHISPER_MODEL_INSTALL_PATH}"
  rm -f "${tmp_model_path}"
}

# Authenticate once and keep credentials warm during install.
init_sudo_session() {
  if [[ "${EUID}" -eq 0 ]]; then
    SUDO_CMD=()
    return
  fi

  require_cmd sudo
  log "Authenticating sudo session once"
  sudo -v

  # Keep sudo ticket fresh while install runs so repeated prompts are avoided.
  (
    while true; do
      sleep 30
      sudo -n true || exit 0
    done
  ) &
  SUDO_KEEPALIVE_PID=$!
}

cleanup_sudo_session() {
  if [[ -n "${SUDO_KEEPALIVE_PID}" ]]; then
    kill "${SUDO_KEEPALIVE_PID}" >/dev/null 2>&1 || true
  fi
}

# Close the multiplexed SSH master connection if we created one.
cleanup_ssh_master() {
  if [[ -n "${SSH_CONTROL_PATH}" && -n "${SSH_MASTER_TARGET}" ]]; then
    ssh -S "${SSH_CONTROL_PATH}" -O exit "${SSH_MASTER_TARGET}" >/dev/null 2>&1 || true
    rm -f "${SSH_CONTROL_PATH}" >/dev/null 2>&1 || true
    if [[ -n "${SSH_CONTROL_DIR}" ]]; then
      rmdir "${SSH_CONTROL_DIR}" >/dev/null 2>&1 || true
    fi
    SSH_CONTROL_PATH=""
    SSH_CONTROL_DIR=""
    SSH_MASTER_TARGET=""
  fi
}

cleanup_exit() {
  cleanup_sudo_session
  cleanup_ssh_master
}

trap cleanup_exit EXIT

# ---------------------------------------------------------------------------
# Bluetooth helpers
# ---------------------------------------------------------------------------

# Validate BT_MAC is a well-formed MAC address.
validate_bt_mac() {
  if [[ ! "${BT_MAC}" =~ ^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$ ]]; then
    echo "Invalid Bluetooth MAC address (expected XX:XX:XX:XX:XX:XX): '${BT_MAC}'" >&2
    exit 1
  fi
}

bt_is_paired() {
  bluetoothctl info "$1" 2>/dev/null | grep -q "Paired: yes"
}

bt_is_connected() {
  bluetoothctl info "$1" 2>/dev/null | grep -q "Connected: yes"
}

# Scan briefly, then pair and trust the device.
# The device must be in discoverable/pairable mode during the scan window.
bt_pair_device() {
  local mac="$1"
  local scan_pid=""

  log "Scanning for ${mac} (${BT_SCAN_SECS}s) — ensure device is in pairing mode"
  bluetoothctl scan on >/dev/null 2>&1 &
  scan_pid="$!"
  sleep "${BT_SCAN_SECS}"
  kill "${scan_pid}" 2>/dev/null || true
  wait "${scan_pid}" 2>/dev/null || true
  bluetoothctl scan off >/dev/null 2>&1 || true

  log "Pairing with ${mac}"
  # pair exits non-zero if already paired; treat that as success.
  bluetoothctl pair "${mac}" || log "Pair step returned non-zero (device may already be paired)"
  bluetoothctl trust "${mac}"
}

# Attempt connection up to BT_CONNECT_ATTEMPTS times with a 3-second back-off.
bt_connect_with_retry() {
  local mac="$1"
  local i=1
  local output=""

  while [[ "${i}" -le "${BT_CONNECT_ATTEMPTS}" ]]; do
    log "Connecting to ${mac} (attempt ${i}/${BT_CONNECT_ATTEMPTS})"
    output="$(bluetoothctl connect "${mac}" 2>/dev/null || true)"
    if echo "${output}" | grep -q "Connection successful"; then
      log "Connected to ${mac}"
      return 0
    fi
    i=$((i + 1))
    sleep 3
  done

  echo "Failed to connect to Bluetooth device ${mac} after ${BT_CONNECT_ATTEMPTS} attempts" >&2
  return 1
}

# Emit the ALSA device string for arecord/aplay given a MAC and stack.
bt_device_string() {
  local mac="$1"
  local stack="$2"
  local device_str=""

  if [[ "${stack}" == "bluealsa" ]]; then
    device_str="bluealsa:DEV=${mac},PROFILE=a2dp"
  elif [[ "${stack}" == "pipewire" ]]; then
    # PipeWire exposes the BT sink via PulseAudio compat layer.
    # Sink name pattern: bluez_output.<mac_with_underscores>.<index>
    local normalized_mac=""
    normalized_mac="$(echo "${mac}" | tr ':' '_' | tr '[:upper:]' '[:lower:]')"
    if command -v pactl >/dev/null 2>&1; then
      local discovered=""
      discovered="$(pactl list sinks short 2>/dev/null | awk '{print $2}' \
        | grep -i "${normalized_mac}" | head -1 || true)"
      if [[ -n "${discovered}" ]]; then
        device_str="pulse:${discovered}"
      fi
    fi
    if [[ -z "${device_str}" ]]; then
      # Predictable fallback — index 1 is typical for A2DP.
      device_str="pulse:bluez_output.${normalized_mac}.1"
      log "WARNING: could not detect PipeWire sink via pactl; using fallback: ${device_str}"
    fi
  fi

  echo "${device_str}"
}

# Play 1 second of silence through the device to verify the stack is functional.
bt_validate_audio() {
  local device_str="$1"
  log "Validating audio output on '${device_str}' (1s silence via aplay)"
  if aplay -D "${device_str}" -f S16_LE -r 16000 -c 1 -d 1 /dev/zero >/dev/null 2>&1; then
    log "Audio validation PASSED"
    return 0
  else
    echo "Audio validation FAILED for device '${device_str}'" >&2
    echo "  Check: is the BT device connected? Is the audio stack service running?" >&2
    return 1
  fi
}

# Full Bluetooth setup phase: packages → service → pair → connect → validate → (optionally) update env.
setup_bluetooth_phase() {
  if [[ -z "${BT_MAC}" ]]; then
    echo "--setup-bluetooth requires --bt-mac <MAC>" >&2
    exit 1
  fi
  validate_bt_mac

  if [[ "${BT_STACK}" != "bluealsa" && "${BT_STACK}" != "pipewire" ]]; then
    echo "Invalid --bt-stack value '${BT_STACK}'. Must be 'bluealsa' or 'pipewire'." >&2
    exit 1
  fi

  log "--- Bluetooth setup: stack=${BT_STACK} mac=${BT_MAC} ---"

  if [[ "${SKIP_PACKAGES}" == "false" ]]; then
    require_cmd apt-get
    if [[ "${BT_STACK}" == "bluealsa" ]]; then
      log "Installing BlueALSA packages"
      "${SUDO_CMD[@]}" apt-get install -y bluez bluez-tools bluealsa alsa-utils
    else
      log "Installing PipeWire Bluetooth packages"
      "${SUDO_CMD[@]}" apt-get install -y bluez bluez-tools pipewire pipewire-pulse \
        libspa-0.2-bluetooth wireplumber alsa-utils
    fi
  fi

  require_cmd bluetoothctl
  require_cmd aplay

  log "Enabling bluetooth service"
  "${SUDO_CMD[@]}" systemctl enable --now bluetooth

  log "Powering on Bluetooth adapter"
  bluetoothctl power on

  if [[ "${BT_STACK}" == "bluealsa" ]]; then
    log "Enabling bluealsa service"
    "${SUDO_CMD[@]}" systemctl enable --now bluealsa
  fi

  if [[ "${SKIP_BT_PAIR}" == "false" ]]; then
    if bt_is_paired "${BT_MAC}"; then
      log "Device ${BT_MAC} already paired — skipping pair step"
    else
      bt_pair_device "${BT_MAC}"
    fi
  fi

  bt_connect_with_retry "${BT_MAC}"

  local device_str=""
  device_str="$(bt_device_string "${BT_MAC}" "${BT_STACK}")"

  bt_validate_audio "${device_str}"

  if [[ "${UPDATE_BT_CONFIG}" == "true" ]]; then
    local env_file="/etc/local-ai-assistant/assistantd.env"
    if [[ ! -f "${env_file}" ]]; then
      log "WARNING: ${env_file} not found; skipping config update"
    elif "${SUDO_CMD[@]}" grep -q "^AUDIO_PLAYBACK_DEVICE=" "${env_file}"; then
      log "Updating AUDIO_PLAYBACK_DEVICE in ${env_file}"
      "${SUDO_CMD[@]}" sed -i "s|^AUDIO_PLAYBACK_DEVICE=.*|AUDIO_PLAYBACK_DEVICE=${device_str}|" "${env_file}"
    else
      log "Appending AUDIO_PLAYBACK_DEVICE to ${env_file}"
      echo "AUDIO_PLAYBACK_DEVICE=${device_str}" | "${SUDO_CMD[@]}" tee -a "${env_file}" >/dev/null
    fi
    log "Env file updated: AUDIO_PLAYBACK_DEVICE=${device_str}"
  fi

  log "--- Bluetooth setup complete ---"
  log "  Device string : ${device_str}"
  log "  Next step     : set AUDIO_PLAYBACK_DEVICE=${device_str} in your env file."
}

# Parse installer arguments.
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ssh)
      SSH_TARGET="${2:-}"
      shift 2
      ;;
    --remote-dir)
      REMOTE_DIR="${2:-}"
      shift 2
      ;;
    --repo-root)
      ROOT_DIR="${2:-}"
      BUILD_DIR="${ROOT_DIR}/build"
      UNIT_SRC="${ROOT_DIR}/systemd/assistantd.service"
      CONFIG_SRC="${ROOT_DIR}/config/assistantd.env.example"
      shift 2
      ;;
    --local-install)
      LOCAL_INSTALL=true
      shift
      ;;
    --skip-packages)
      SKIP_PACKAGES=true
      shift
      ;;
    --skip-enable)
      SKIP_ENABLE=true
      shift
      ;;
    --no-build)
      NO_BUILD=true
      shift
      ;;
    --config-src)
      CONFIG_SRC="${2:-}"
      shift 2
      ;;
    --service-user)
      SERVICE_USER="${2:-}"
      shift 2
      ;;
    --service-group)
      SERVICE_GROUP="${2:-}"
      shift 2
      ;;
    --ssh-no-tty)
      SSH_TTY=false
      shift
      ;;
    --setup-bluetooth)
      SETUP_BLUETOOTH=true
      shift
      ;;
    --bt-mac)
      BT_MAC="${2:-}"
      shift 2
      ;;
    --bt-stack)
      BT_STACK="${2:-}"
      shift 2
      ;;
    --skip-bt-pair)
      SKIP_BT_PAIR=true
      shift
      ;;
    --update-bt-config)
      UPDATE_BT_CONFIG=true
      shift
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

# SSH deploy mode:
# 1) Open one reusable SSH control connection.
# 2) Sync repo contents to the target host.
# 3) Invoke this same installer remotely in --local-install mode.
if [[ -n "${SSH_TARGET}" && "${LOCAL_INSTALL}" == "false" ]]; then
  require_cmd rsync
  require_cmd ssh
  require_cmd mktemp

  if [[ -z "${REMOTE_DIR}" ]]; then
    if [[ "${SSH_TARGET}" == *"@"* ]]; then
      remote_user="${SSH_TARGET%%@*}"
      REMOTE_DIR="/home/${remote_user}/assistantd"
    else
      REMOTE_DIR="/tmp/assistantd"
    fi
  fi

  # Reuse one authenticated SSH session for rsync + remote command execution.
  SSH_CONTROL_DIR="$(mktemp -d "${TMPDIR:-/tmp}/assistantd-ssh-XXXXXXXX")"
  SSH_CONTROL_PATH="${SSH_CONTROL_DIR}/control.sock"
  SSH_MASTER_TARGET="${SSH_TARGET}"
  log "Opening SSH control connection to ${SSH_TARGET}"
  ssh -MNf \
    -o ControlMaster=yes \
    -o ControlPersist=10m \
    -o ControlPath="${SSH_CONTROL_PATH}" \
    "${SSH_TARGET}"

  local_src="${ROOT_DIR%/}/"
  remote_dst="${SSH_TARGET}:${REMOTE_DIR%/}/"
  log "Syncing repository to ${remote_dst}"
  rsync -az --delete \
    --exclude ".git/" \
    --exclude "build/" \
    --exclude "cmake-build-*/" \
    -e "ssh -o ControlMaster=auto -o ControlPersist=10m -o ControlPath=${SSH_CONTROL_PATH}" \
    "${local_src}" "${remote_dst}"

  # Forward relevant toggles to the remote installer invocation.
  remote_cmd=(
    bash "${REMOTE_DIR%/}/scripts/install_assistantd.sh"
    --local-install
    --repo-root "${REMOTE_DIR%/}"
    --service-user "${SERVICE_USER}"
    --service-group "${SERVICE_GROUP}"
  )
  if [[ "${SKIP_PACKAGES}" == "true" ]]; then
    remote_cmd+=(--skip-packages)
  fi
  if [[ "${SKIP_ENABLE}" == "true" ]]; then
    remote_cmd+=(--skip-enable)
  fi
  if [[ "${NO_BUILD}" == "true" ]]; then
    remote_cmd+=(--no-build)
  fi
  if [[ "${SETUP_BLUETOOTH}" == "true" ]]; then
    remote_cmd+=(--setup-bluetooth)
  fi
  if [[ -n "${BT_MAC}" ]]; then
    remote_cmd+=(--bt-mac "${BT_MAC}")
  fi
  if [[ "${BT_STACK}" != "bluealsa" ]]; then
    remote_cmd+=(--bt-stack "${BT_STACK}")
  fi
  if [[ "${SKIP_BT_PAIR}" == "true" ]]; then
    remote_cmd+=(--skip-bt-pair)
  fi
  if [[ "${UPDATE_BT_CONFIG}" == "true" ]]; then
    remote_cmd+=(--update-bt-config)
  fi

  # Run remote install with optional TTY so remote sudo can prompt.
  quoted_remote_cmd="$(printf "%q " "${remote_cmd[@]}")"
  log "Running remote installer on ${SSH_TARGET} via SSH control connection"
  ssh_cmd=(
    ssh
    -o ControlMaster=auto
    -o ControlPersist=10m
    -o ControlPath="${SSH_CONTROL_PATH}"
  )
  if [[ "${SSH_TTY}" == "true" ]]; then
    # Force TTY so remote sudo can prompt for password when needed.
    ssh_cmd+=(-tt)
  fi
  ssh_cmd+=("${SSH_TARGET}" "${quoted_remote_cmd}")
  "${ssh_cmd[@]}"
  log "Remote install completed on ${SSH_TARGET}"
  exit 0
fi

# Local install mode (either directly on host or called via SSH deploy mode).
if [[ ! -f "${UNIT_SRC}" ]]; then
  echo "Cannot find systemd unit at ${UNIT_SRC}" >&2
  exit 1
fi
if [[ ! -f "${CONFIG_SRC}" ]]; then
  echo "Cannot find config template at ${CONFIG_SRC}" >&2
  exit 1
fi

# Prime sudo credentials once before privileged operations.
init_sudo_session

# Install build/runtime packages if requested.
if [[ "${SKIP_PACKAGES}" == "false" ]]; then
  require_cmd apt-get
  log "Installing build/runtime dependencies via apt-get"
  "${SUDO_CMD[@]}" apt-get update
  "${SUDO_CMD[@]}" apt-get install -y \
    build-essential \
    cmake \
    curl \
    ca-certificates \
    alsa-utils \
    libcurl4-openssl-dev
fi

log "Ensuring whisper-cli runtime dependency"
install_whisper_cli
log "Ensuring whisper model artifact"
install_whisper_model

# Build the daemon unless caller provided --no-build.
if [[ "${NO_BUILD}" == "false" ]]; then
  require_cmd cmake
  log "Configuring CMake project"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
  log "Building assistantd"
  cmake --build "${BUILD_DIR}" --target assistantd
fi

BINARY_PATH="${BUILD_DIR}/assistantd"
if [[ ! -x "${BINARY_PATH}" ]]; then
  echo "Built binary not found: ${BINARY_PATH}" >&2
  exit 1
fi

# Install binary and required runtime directories.
log "Installing binary and runtime directories"
"${SUDO_CMD[@]}" install -m 755 "${BINARY_PATH}" /usr/local/bin/assistantd
"${SUDO_CMD[@]}" install -d -m 755 /etc/local-ai-assistant
"${SUDO_CMD[@]}" install -d -m 755 /var/lib/local-ai-assistant

if [[ ! -f /etc/local-ai-assistant/assistantd.env ]]; then
  log "Installing default config to /etc/local-ai-assistant/assistantd.env"
  "${SUDO_CMD[@]}" install -m 644 "${CONFIG_SRC}" /etc/local-ai-assistant/assistantd.env
else
  # Preserve existing config on upgrades.
  log "Keeping existing config at /etc/local-ai-assistant/assistantd.env"
fi

# Render unit with selected service user/group, then install it.
tmp_unit="$(mktemp)"
sed \
  -e "s/^User=.*/User=${SERVICE_USER}/" \
  -e "s/^Group=.*/Group=${SERVICE_GROUP}/" \
  "${UNIT_SRC}" > "${tmp_unit}"
"${SUDO_CMD[@]}" install -m 644 "${tmp_unit}" /etc/systemd/system/assistantd.service
rm -f "${tmp_unit}"

log "Reloading systemd units"
"${SUDO_CMD[@]}" systemctl daemon-reload

# Run Bluetooth setup before the service starts so the env file is correct when it does.
if [[ "${SETUP_BLUETOOTH}" == "true" ]]; then
  setup_bluetooth_phase
fi

# Enable and start service by default, unless explicitly disabled.
if [[ "${SKIP_ENABLE}" == "true" ]]; then
  log "Skipping service enable/start (--skip-enable)"
else
  log "Enabling and starting assistantd"
  "${SUDO_CMD[@]}" systemctl enable --now assistantd
  "${SUDO_CMD[@]}" systemctl status assistantd --no-pager || true
fi

log "Install finished."
