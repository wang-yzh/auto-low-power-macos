#!/usr/bin/env bash

set -euo pipefail

REPO="wang-yzh/auto-low-power-macos"
API_URL="https://api.github.com/repos/${REPO}/releases/latest"
WORK_DIR="$(mktemp -d)"
ARCHIVE="${WORK_DIR}/release.tar.gz"
EXTRACT_DIR="${WORK_DIR}/repo"

cleanup() {
  rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required."
  exit 1
fi

if ! command -v tar >/dev/null 2>&1; then
  echo "tar is required."
  exit 1
fi

LATEST_TAG="$(curl -fsSL "${API_URL}" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n 1)"
if [[ -z "${LATEST_TAG}" ]]; then
  echo "Failed to resolve latest release tag."
  exit 1
fi

ARCHIVE_URL="https://github.com/${REPO}/releases/download/${LATEST_TAG}/auto-low-power-macos-${LATEST_TAG}.tar.gz"

mkdir -p "${EXTRACT_DIR}"
curl -fsSL "${ARCHIVE_URL}" -o "${ARCHIVE}"
tar -xzf "${ARCHIVE}" -C "${EXTRACT_DIR}" --strip-components=1

cd "${EXTRACT_DIR}"
sudo env \
  AUTO_LOW_POWER_THRESHOLD="${AUTO_LOW_POWER_THRESHOLD:-25}" \
  AUTO_LOW_POWER_DEBUG="${AUTO_LOW_POWER_DEBUG:-0}" \
  ./scripts/install.sh
