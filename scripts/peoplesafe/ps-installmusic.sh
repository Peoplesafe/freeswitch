#!/bin/bash

# Source common paths
source "$(dirname "$0")/ps-constants.sh"

SOUNDS_VERSION=freeswitch-sounds-music-8000-1.0.52

if [ ! -d "${SOUNDS_DIR}" ]; then
    sudo mkdir -p "${SOUNDS_DIR}" || exit 1
fi
cd ${SOUNDS_DIR} || exit 1

echo "=== Installing FreeSWITCH Sounds ==="
sudo wget http://files.freeswitch.org/releases/sounds/${SOUNDS_VERSION}.tar.gz
sudo tar -xzf ${SOUNDS_VERSION}.tar.gz || exit 1
sudo rm -f ${SOUNDS_VERSION}.tar.gz || exit 1
echo "Sounds extracted to ${SOUNDS_DIR}"
echo "Copying sounds to FreeSWITCH installation directory..."
sudo cp -r music ${FREESWITCH_INSTALL_DIR}/sounds
echo "Setting ownership and permissions for sounds directory..."
sudo chown -R ${FS_USER}:${FS_GROUP} ${FREESWITCH_INSTALL_DIR}/sounds/music || exit 1