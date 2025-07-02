#!/bin/bash

# Source common paths
source "$(dirname "$0")/ps-constants.sh"

SDK_TAR="azure-speech-sdk.tar.gz"

echo "=== Installing Azure Speech SDK ==="
if [ ! -d "${SPEECH_SDK_DIR}" ]; then
    sudo mkdir -p "${SPEECH_SDK_DIR}" || exit 1
fi

cd "${SPEECH_SDK_DIR}" || exit 1

export SPEECHSDK_ROOT="${SPEECH_SDK_DIR}"
sudo wget -O ${SDK_TAR} https://aka.ms/csspeech/linuxbinary || exit 1
sudo tar --strip 1 -xzf ${SDK_TAR} -C "$SPEECHSDK_ROOT"

sudo rm -f ${SDK_TAR} || exit 1
echo "Azure Speech SDK extracted to ${SPEECH_SDK_DIR}"

echo "Setting ownership and permissions for Azure Speech SDK directory..."
sudo chown -R psadmin:psadmin "${SPEECH_SDK_DIR}" || exit 1
sudo chmod -R 755 "${SPEECH_SDK_DIR}" || exit 1
echo "Azure Speech SDK installation completed successfully."