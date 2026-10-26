#!/usr/bin/env bash
# extract_assets.sh
# Downloads and extracts official UltraViewer assets (specifically 8bit.dat dictionary)
# required for Zstandard screen decoding in remmina-plugin-ultraviewer.

set -euo pipefail

EXPECTED_SHA256="9aa45789d9ff9d329a623a685d57b67e33c777019de9ce1f273f21860e4bfe6b"
EXPECTED_SIZE=112640
INSTALLER_URL="https://dl2.ultraviewer.net/UltraViewer_setup_6.6.133_vi.exe"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TARGET_DIR="${1:-${REPO_ROOT}/assets}"
TARGET_FILE="${TARGET_DIR}/8bit.dat"

verify_checksum() {
    local file_path="$1"
    if [ ! -f "${file_path}" ]; then
        return 1
    fi

    local actual_size
    actual_size=$(wc -c < "${file_path}" | tr -d ' ')
    if [ "${actual_size}" -ne "${EXPECTED_SIZE}" ]; then
        return 1
    fi

    local actual_sha256
    actual_sha256=$(sha256sum "${file_path}" | awk '{print $1}')
    if [ "${actual_sha256}" = "${EXPECTED_SHA256}" ]; then
        return 0
    else
        return 1
    fi
}

mkdir -p "${TARGET_DIR}"

# 1. Check if asset already exists and matches expected checksum
if verify_checksum "${TARGET_FILE}"; then
    echo "[✓] Valid 8bit.dat already exists at: ${TARGET_FILE}"
    exit 0
fi

echo "[*] Searching for local 8bit.dat in nearby project checkouts..."

# 2. Check local sibling MacViewer repositories
LOCAL_CANDIDATES=(
    "${REPO_ROOT}/../MacViewer/LinuxViewer/assets/8bit.dat"
    "${REPO_ROOT}/../MacViewer/Sources/UltraViewerProtocol/Resources/8bit.dat"
    "${REPO_ROOT}/../../MacViewer/LinuxViewer/assets/8bit.dat"
    "${REPO_ROOT}/../../MacViewer/Sources/UltraViewerProtocol/Resources/8bit.dat"
)

for candidate in "${LOCAL_CANDIDATES[@]}"; do
    if [ -f "${candidate}" ] && verify_checksum "${candidate}"; then
        echo "[+] Found verified 8bit.dat at: ${candidate}"
        cp "${candidate}" "${TARGET_FILE}"
        echo "[✓] Successfully installed to: ${TARGET_FILE}"
        exit 0
    fi
done

# 3. Download official UltraViewer installer and extract
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

INSTALLER_PATH="${TMP_DIR}/UltraViewer_setup.exe"
echo "[*] Downloading UltraViewer installer from: ${INSTALLER_URL}..."

if command -v curl >/dev/null 2>&1; then
    curl -fL --progress-bar -o "${INSTALLER_PATH}" "${INSTALLER_URL}"
elif command -v wget >/dev/null 2>&1; then
    wget -q --show-progress -O "${INSTALLER_PATH}" "${INSTALLER_URL}"
else
    echo "[-] Error: Neither curl nor wget is available to download installer." >&2
    exit 1
fi

echo "[*] Extracting 8bit.dat from installer..."

EXTRACTED=0

# Try innoextract if available
if command -v innoextract >/dev/null 2>&1; then
    echo "[*] Trying innoextract..."
    if innoextract -e -s --output-dir "${TMP_DIR}/inno_out" "${INSTALLER_PATH}" >/dev/null 2>&1; then
        FOUND=$(find "${TMP_DIR}/inno_out" -name "8bit.dat" | head -n 1)
        if [ -n "${FOUND}" ] && verify_checksum "${FOUND}"; then
            cp "${FOUND}" "${TARGET_FILE}"
            EXTRACTED=1
        fi
    fi
fi

# Fallback to 7z
if [ "${EXTRACTED}" -eq 0 ] && command -v 7z >/dev/null 2>&1; then
    echo "[*] Trying 7z..."
    if 7z e -o"${TMP_DIR}/7z_out" "${INSTALLER_PATH}" "8bit.dat" -r -y >/dev/null 2>&1; then
        FOUND=$(find "${TMP_DIR}/7z_out" -name "8bit.dat" | head -n 1)
        if [ -n "${FOUND}" ] && verify_checksum "${FOUND}"; then
            cp "${FOUND}" "${TARGET_FILE}"
            EXTRACTED=1
        fi
    fi
fi

if [ "${EXTRACTED}" -eq 1 ] && verify_checksum "${TARGET_FILE}"; then
    echo "[✓] Successfully extracted and verified: ${TARGET_FILE}"
    exit 0
else
    echo "[-] Error: Failed to extract a valid 8bit.dat from ${INSTALLER_URL}." >&2
    echo "    Please verify that 'innoextract' or '7z' is installed." >&2
    exit 1
fi
