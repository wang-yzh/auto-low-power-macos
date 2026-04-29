#!/usr/bin/env bash

set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run this script with sudo."
  exit 1
fi

ROOT_DIR="${AUTO_LOW_POWER_ROOT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
BUILD_DIR="${ROOT_DIR}/build"
SRC_FILE="${ROOT_DIR}/src/auto_low_power_listener.c"
TEMPLATE_PLIST="${ROOT_DIR}/launchd/io.github.autolowpower.macos.plist"

LABEL="io.github.autolowpower.macos"
INSTALL_DIR="/Library/Application Support/AutoLowPower"
INSTALL_BIN="${INSTALL_DIR}/auto-low-power-listener"
INSTALL_PLIST="/Library/LaunchDaemons/${LABEL}.plist"
BUILD_BIN="${BUILD_DIR}/auto-low-power-listener"
BUILD_PLIST="${BUILD_DIR}/${LABEL}.plist"

THRESHOLD="${AUTO_LOW_POWER_THRESHOLD:-25}"
DEBUG="${AUTO_LOW_POWER_DEBUG:-0}"
COOLDOWN="${AUTO_LOW_POWER_APPLY_COOLDOWN_SECONDS:-3}"

mkdir -p "${BUILD_DIR}"

clang -O2 -Wall -Wextra -framework CoreFoundation -framework IOKit \
  -o "${BUILD_BIN}" "${SRC_FILE}"

sed \
  -e "s|__THRESHOLD__|${THRESHOLD}|g" \
  -e "s|__DEBUG__|${DEBUG}|g" \
  -e "s|__COOLDOWN__|${COOLDOWN}|g" \
  "${TEMPLATE_PLIST}" > "${BUILD_PLIST}"

install -d -m 755 "${INSTALL_DIR}"
install -m 755 "${BUILD_BIN}" "${INSTALL_BIN}"
install -m 644 "${BUILD_PLIST}" "${INSTALL_PLIST}"

launchctl bootout "system/${LABEL}" 2>/dev/null || true
launchctl bootstrap system "${INSTALL_PLIST}"
launchctl enable "system/${LABEL}"
launchctl kickstart -k "system/${LABEL}"

echo "Installed ${LABEL} with threshold ${THRESHOLD}%."
