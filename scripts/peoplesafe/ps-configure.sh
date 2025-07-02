#!/bin/bash

# FreeSWITCH Configuration Script
# Run this from the FreeSWITCH source directory: ./fs-configure.sh

set -e  # Exit on any error

# Source common paths
source "$(dirname "$0")/ps-constants.sh"

echo "=== FreeSWITCH Configuration Script ==="
echo "Configuring FreeSWITCH with custom paths..."

cd ${FREESWITCH_SRC}

# Check if we're in the right directory
if [ ! -f "configure.ac" ] || [ ! -f "bootstrap.sh" ]; then
    echo "Error: This script must be run from the FreeSWITCH source directory"
    echo "Current directory: $(pwd)"
    echo "Please cd to your FreeSWITCH source directory and run again"
    exit 1
fi

# Run configure with your custom options
./configure \
    --prefix=${FREESWITCH_INSTALL_DIR} \
    --sysconfdir=${FREESWITCH_INSTALL_DIR}/conf \
    --localstatedir=${FREESWITCH_INSTALL_DIR}/var \
    --with-rundir=${FREESWITCH_INSTALL_DIR}/run \
    --with-logdir=${FREESWITCH_INSTALL_DIR}/log \
    --with-dbdir=${FREESWITCH_INSTALL_DIR}/db \
    --with-htdocsdir=${FREESWITCH_INSTALL_DIR}/htdocs \
    --with-soundsdir=${FREESWITCH_INSTALL_DIR}/sounds \
    --with-grammardir=${FREESWITCH_INSTALL_DIR}/grammar \
    --with-certsdir=${FREESWITCH_INSTALL_DIR}/certs \
    --with-scriptdir=${FREESWITCH_INSTALL_DIR}/scripts \
    --with-recordingsdir=${FREESWITCH_INSTALL_DIR}/recordings \
    --enable-core-pgsql-support=no \
    --enable-core-odbc-support=no \
    --disable-dependency-tracking \
    --without-spandsp \

echo "=== Configuration Complete ==="