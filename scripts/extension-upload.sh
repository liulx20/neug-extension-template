#!/bin/bash
set -euo pipefail

# Upload a NeuG extension binary to OSS.
#
# Usage: ./extension-upload.sh <name> <extension_version> <neug_version> <platform> <oss_prefix>
#
# Example:
#   ./extension-upload.sh wiggle abc1234 0.1.3 linux_x86_64 oss://graphscope/neug/extensions

if [ "$#" -lt 5 ]; then
  echo "Usage: $0 <name> <extension_version> <neug_version> <platform> <oss_prefix>"
  exit 1
fi

EXT_NAME="$1"
EXT_VERSION="$2"
NEUG_VERSION="$3"
PLATFORM="$4"
OSS_PREFIX="$5"

EXT_FILE="lib${EXT_NAME}.neug_extension"
LOCAL_PATH="${BUILD_DIR:-./build/release}/extension/${EXT_NAME}/${EXT_FILE}"

if [ ! -f "${LOCAL_PATH}" ]; then
  echo "Extension binary not found: ${LOCAL_PATH}"
  exit 1
fi

REMOTE_PATH="${OSS_PREFIX}/v${NEUG_VERSION}/${PLATFORM}/${EXT_FILE}"
echo "Uploading ${LOCAL_PATH} -> ${REMOTE_PATH}"

if command -v ossutil >/dev/null 2>&1; then
  ossutil cp "${LOCAL_PATH}" "${REMOTE_PATH}"
elif command -v aws >/dev/null 2>&1; then
  aws s3 cp "${LOCAL_PATH}" "${REMOTE_PATH}"
else
  echo "Neither ossutil nor aws CLI found. Upload manually to ${REMOTE_PATH}"
  exit 1
fi

echo "Done."
