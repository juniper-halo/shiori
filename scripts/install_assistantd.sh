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

# Enable and start service by default, unless explicitly disabled.
if [[ "${SKIP_ENABLE}" == "true" ]]; then
  log "Skipping service enable/start (--skip-enable)"
else
  log "Enabling and starting assistantd"
  "${SUDO_CMD[@]}" systemctl enable --now assistantd
  "${SUDO_CMD[@]}" systemctl status assistantd --no-pager || true
fi

log "Install finished."
