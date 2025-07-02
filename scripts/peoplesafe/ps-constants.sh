#!/bin/bash

# Common configuration for FreeSWITCH scripts
# Source this file in other scripts: source "$(dirname "$0")/ps-constants.sh"

# Source paths
FREESWITCH_SRC="/usr/local/src/freeswitch"
SOUNDS_DIR="/usr/local/src/sounds"

# Install paths
FREESWITCH_INSTALL_DIR="/usr/local/freeswitch"
MODULE_BUILD_DIR="${FREESWITCH_INSTALL_DIR}/lib/freeswitch/mod/"
BIN_PATH="${FREESWITCH_INSTALL_DIR}/bin"
LOG_PATH="${FREESWITCH_INSTALL_DIR}/var/log/freeswitch"
RUN_PATH="${FREESWITCH_INSTALL_DIR}/run"
CONF_PATH="${FREESWITCH_INSTALL_DIR}/conf/freeswitch"

# External library paths
SPEECH_SDK_DIR="/usr/local/lib/speechsdk"

# User and group
FS_USER="freeswitch"
FS_GROUP="freeswitch"

# Service name
SERVICE_NAME="freeswitch"