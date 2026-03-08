#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
UNIT_SRC="${ROOT_DIR}/systemd/assistantd.service"
CONFIG_SRC="${ROOT_DIR}/config/assistantd.env.example"

SSH_TARGET=""
REMOTE_DIR=""
LOCAL_INSTALL=false
SKIP_PACKAGES=false
SKIP_ENABLE=false
NO_BUILD=false
SERVICE_USER="pi"
SERVICE_GROUP="pi"

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
  --help                 Show help.
EOF
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

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

if [[ -n "${SSH_TARGET}" && "${LOCAL_INSTALL}" == "false" ]]; then
  require_cmd rsync
  require_cmd ssh

  if [[ -z "${REMOTE_DIR}" ]]; then
    if [[ "${SSH_TARGET}" == *"@"* ]]; then
      remote_user="${SSH_TARGET%%@*}"
      REMOTE_DIR="/home/${remote_user}/assistantd"
    else
      REMOTE_DIR="/tmp/assistantd"
    fi
  fi

  local_src="${ROOT_DIR%/}/"
  remote_dst="${SSH_TARGET}:${REMOTE_DIR%/}/"
  log "Syncing repository to ${remote_dst}"
  rsync -az --delete \
    --exclude ".git/" \
    --exclude "build/" \
    --exclude "cmake-build-*/" \
    "${local_src}" "${remote_dst}"

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

  quoted_remote_cmd="$(printf "%q " "${remote_cmd[@]}")"
  log "Running remote installer on ${SSH_TARGET}"
  ssh "${SSH_TARGET}" "${quoted_remote_cmd}"
  log "Remote install completed on ${SSH_TARGET}"
  exit 0
fi

if [[ ! -f "${UNIT_SRC}" ]]; then
  echo "Cannot find systemd unit at ${UNIT_SRC}" >&2
  exit 1
fi
if [[ ! -f "${CONFIG_SRC}" ]]; then
  echo "Cannot find config template at ${CONFIG_SRC}" >&2
  exit 1
fi

if [[ "${SKIP_PACKAGES}" == "false" ]]; then
  require_cmd sudo
  require_cmd apt-get
  log "Installing build/runtime dependencies via apt-get"
  sudo apt-get update
  sudo apt-get install -y build-essential cmake
fi

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

log "Installing binary and runtime directories"
sudo install -m 755 "${BINARY_PATH}" /usr/local/bin/assistantd
sudo install -d -m 755 /etc/local-ai-assistant
sudo install -d -m 755 /var/lib/local-ai-assistant

if [[ ! -f /etc/local-ai-assistant/assistantd.env ]]; then
  log "Installing default config to /etc/local-ai-assistant/assistantd.env"
  sudo install -m 644 "${CONFIG_SRC}" /etc/local-ai-assistant/assistantd.env
else
  log "Keeping existing config at /etc/local-ai-assistant/assistantd.env"
fi

tmp_unit="$(mktemp)"
sed \
  -e "s/^User=.*/User=${SERVICE_USER}/" \
  -e "s/^Group=.*/Group=${SERVICE_GROUP}/" \
  "${UNIT_SRC}" > "${tmp_unit}"
sudo install -m 644 "${tmp_unit}" /etc/systemd/system/assistantd.service
rm -f "${tmp_unit}"

log "Reloading systemd units"
sudo systemctl daemon-reload

if [[ "${SKIP_ENABLE}" == "true" ]]; then
  log "Skipping service enable/start (--skip-enable)"
else
  log "Enabling and starting assistantd"
  sudo systemctl enable --now assistantd
  sudo systemctl status assistantd --no-pager || true
fi

log "Install finished."
