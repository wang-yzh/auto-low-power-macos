#!/usr/bin/env bash

set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run this script with sudo."
  exit 1
fi

LABEL="io.github.autolowpower.macos"
INSTALL_DIR="/Library/Application Support/AutoLowPower"
INSTALL_BIN="${INSTALL_DIR}/auto-low-power-listener"
INSTALL_PLIST="/Library/LaunchDaemons/${LABEL}.plist"

launchctl bootout "system/${LABEL}" 2>/dev/null || true
rm -f "${INSTALL_PLIST}"
rm -f "${INSTALL_BIN}"
rmdir "${INSTALL_DIR}" 2>/dev/null || true

echo "Removed ${LABEL}."
