#!/bin/bash

# FreeSWITCH Post-Installation Setup Script
# Run this after 'make install' and 'make samples'

set -e  # Exit on any error

# Source common paths
source "$(dirname "$0")/ps-constants.sh"

# Stop FreeSWITCH"
sudo systemctl stop ${SERVICE_NAME}     

echo "=== FreeSWITCH Post-Installation Setup ==="
echo "Install path: ${FREESWITCH_INSTALL_DIR}"

# Check if FreeSWITCH is installed
if [ ! -f "${BIN_PATH}/freeswitch" ]; then
    echo "Error: FreeSWITCH not found at ${BIN_PATH}/freeswitch"
    echo "Please run 'make install' first"
    exit 1
fi

echo "1. Creating ${FS_USER} user and group..."
if ! getent group ${FS_GROUP} > /dev/null 2>&1; then
    sudo groupadd --system ${FS_GROUP}
    echo "   Created group: ${FS_GROUP}"
else
    echo "   Group '${FS_GROUP}' already exists"
fi

if ! getent passwd ${FS_USER} > /dev/null 2>&1; then
    sudo useradd --system -g ${FS_GROUP} --home-dir ${FREESWITCH_INSTALL_DIR} --shell /bin/false ${FS_USER}
    echo "   Created user: ${FS_USER}"
else
    echo "   User '${FS_USER}' already exists"
fi

# Force ownership to freeswitch user and group
echo "2. Setting ownership and permissions..."
sudo chown -R ${FS_USER}:${FS_GROUP} ${FREESWITCH_INSTALL_DIR}
sudo chmod -R u=rwX,g=rX,o= ${FREESWITCH_INSTALL_DIR}
echo "   Ownership set to ${FS_USER}:${FS_GROUP}"

# Service file ExecStartPre command also ensures ownership
echo "3. Creating systemd service file..."
sudo tee /etc/systemd/system/${SERVICE_NAME}.service > /dev/null << EOF
[Unit]
Description=FreeSWITCH
After=network-online.target
Wants=network-online.target
RequiresMountsFor=${FREESWITCH_INSTALL_DIR}

[Service]
Type=forking
PIDFile=${RUN_PATH}/freeswitch.pid
User=${FS_USER}
Group=${FS_GROUP}
EnvironmentFile=-/etc/default/${SERVICE_NAME}

# Startup commands
ExecStartPre=/bin/chown -R ${FS_USER}:${FS_GROUP} ${FREESWITCH_INSTALL_DIR}
ExecStart=${BIN_PATH}/freeswitch -u ${FS_USER} -g ${FS_GROUP} -ncwait -nonat -nonatmap -nocal -nort

TimeoutSec=45s
Restart=always
WorkingDirectory=${FREESWITCH_INSTALL_DIR}


# Security
#NoNewPrivileges=yes
#PrivateTmp=yes
#ProtectHome=yes
#ProtectSystem=strict
#ReadWritePaths=${FREESWITCH_INSTALL_DIR}

[Install]
WantedBy=multi-user.target
EOF

echo "   Systemd service file created"

echo "4. Enabling systemd service..."
sudo systemctl daemon-reload
sudo systemctl enable ${SERVICE_NAME}
echo "   Service enabled"

echo "5. Adding FreeSWITCH CLI to PATH.."
if ! grep -q "${BIN_PATH}" ~/.bashrc; then
    echo "export PATH=\"${BIN_PATH}:\$PATH\"" >> ~/.bashrc
    echo "   Added to ~/.bashrc (reload shell or run: source ~/.bashrc)"
else
    echo "   Already in PATH"
fi


echo "6. Configure Jails..."

#Faile2Ban will read logs and block IPs based on patterns, but we need to ensure the log paths are correct.

# Configure Fail2Ban jail for Dial Plan
JAIL_NAME="freeswitch-dialplan"
CONFIG_FILE="/etc/fail2ban/jail.local"
echo "   ${JAIL_NAME} jail configuration"
sudo sed -i "/^\[${JAIL_NAME}\]/,/^\[/ {
    s|^logpath = .*|logpath = ${LOG_PATH}/freeswitch.log|
}" "$CONFIG_FILE"

# Configure Fail2Ban jail for Dial Plan
JAIL_NAME="freeswitch"
CONFIG_FILE="/etc/fail2ban/jail.d/freeswitch.conf"
echo "   ${JAIL_NAME} jail configuration"
sudo sed -i "/^\[${JAIL_NAME}\]/,/^\[/ {
    s|^logpath = .*|logpath = ${LOG_PATH}/freeswitch.log|
}" "$CONFIG_FILE"

sudo fail2ban-client reload

echo "7. Configure Freeswitch vars..."
sudo sed -i "s/data=\"default_password=[^\"]*\"/data=\"default_password=sajhfgnvy4td4\"/" ${CONF_PATH}/vars.xml

sudo sed -i "s/data=\"domain=[^\"]*\"/data=\"domain=parlez.crispy-duck.co.uk\"/" ${CONF_PATH}/vars.xml
sudo sed -i "s/data=\"domain_name=[^\"]*\"/data=\"domain_name=\$\${domain}\"/" ${CONF_PATH}/vars.xml

echo "6.Configure Permissions..."
# Set permissions for FreeSWITCH directories
sudo chmod -R g+w ${CONF_PATH}

echo "=== Completed Setup ==="