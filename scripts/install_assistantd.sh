#!/usr/bin/env bash
set -euo pipefail

# TODO(implementation-playbook):
# Purpose/ownership:
# - Platform team script to install and register assistantd service on Raspberry Pi OS.
# Inputs/outputs:
# - Inputs: built assistantd binary, config file, systemd unit.
# - Outputs: /usr/local/bin/assistantd, /etc/local-ai-assistant/assistantd.env, enabled service.
# State/concurrency:
# - Idempotent execution; safe to rerun without duplicating side effects.
# Error taxonomy:
# - Explicit non-zero exit for package install failure, copy/install failure, or service start failure.
# Child process contracts:
# - apt/systemctl commands must be logged and validated.
# Acceptance:
# - Running this script on a clean Pi installs dependencies and leaves service enabled+running.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BINARY_PATH="${BUILD_DIR}/assistantd"
UNIT_SRC="${ROOT_DIR}/systemd/assistantd.service"
CONFIG_SRC="${ROOT_DIR}/config/assistantd.env.example"

echo "[scaffold] install script placeholder"
echo "[scaffold] ROOT_DIR=${ROOT_DIR}"
echo "[scaffold] Expected binary: ${BINARY_PATH}"

echo "[scaffold] TODO: install OS packages for audio/model runtime"
echo "[scaffold] TODO: cmake configure/build in ${BUILD_DIR}"
echo "[scaffold] TODO: install ${BINARY_PATH} -> /usr/local/bin/assistantd"
echo "[scaffold] TODO: install ${UNIT_SRC} -> /etc/systemd/system/assistantd.service"
echo "[scaffold] TODO: install ${CONFIG_SRC} -> /etc/local-ai-assistant/assistantd.env"
echo "[scaffold] TODO: run systemctl daemon-reload && enable --now assistantd"
