#!/bin/bash

# Source common paths
source "$(dirname "$0")/ps-constants.sh"

# Define colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
MAGENTA='\033[35m'
WHITE='\033[0;37m'
NC='\033[0m' # No Color

# Simple script with just module name parameter
show_usage() {
    echo "Module name: $0 -m <module name>"
    echo "Configure: $0 -c"
    echo "type: $0 -t"
}

# Initialize variables
CONFIGURE=false
MODULE_NAME=""
MODULE_TYPE="applications"
FORCE_CLEAN=false
# Parse command line options
while getopts "cfm:t:h" opt; do
    case $opt in
        c)
            CONFIGURE=true
            ;;
        f)
            FORCE_CLEAN=true
            ;;    
        m)
            MODULE_NAME="$OPTARG"
            ;;
        t)
            MODULE_TYPE="$OPTARG"
            ;;    
        h)
            show_usage
            exit 0
            ;;
        \?)
            echo -e "${RED}Invalid option: -${OPTARG}${NC}" >&2
            show_usage
            exit 1
            ;;
    esac
done

# Check if module name is provided
if [ -z "$MODULE_NAME" ]; then
    echo "Error: Module name is required"
    show_usage
    exit 1
fi

# Set build paths
MODULE_SRC="${FREESWITCH_SRC}/src/mod/${MODULE_TYPE}/${MODULE_NAME}"

# Check if module source directory exists
if [ ! -d "$MODULE_SRC" ]; then
    echo -e "${RED}Error: Module source directory does not exist: ${MODULE_SRC}${NC}"
    exit 1
fi

# Configure if requested
if [ "$CONFIGURE" = true ]; then
    echo -e "${YELLOW}Configuring the build environment...${NC}"
    ./ps-configure.sh
    if [ $? -ne 0 ]; then
        echo -e "${RED}Configuration failed${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}Skipping configuration step...${NC}"
fi



echo -e "${BLUE}Compiling ${MODULE_NAME}...${NC}"
cd "$FREESWITCH_SRC" || exit 1
# Build directly in the module directory
if [ "$FORCE_CLEAN" = true ]; then
    make ${MODULE_NAME}-clean
    if [ $? -ne 0 ]; then
        echo -e "${RED}Build failed${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}Skipping force clean step...${NC}"
fi

# Build the module directly in its source directory
echo -e "${BLUE}Compiling ${MODULE_NAME}...${NC}"
cd "$FREESWITCH_SRC" || exit 1
# Build directly in the module directory
make ${MODULE_NAME} V=1
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

BACKUP_NAME=freeswitch.backup/$(date +%Y%m%d-%H%M%S)
echo -e "${MAGENTA}Backing up to ${BACKUP_NAME}...${NC}"
sudo cp -r ${FREESWITCH_INSTALL_DIR}/ /usr/local/${BACKUP_NAME}
sudo chown -R ${FS_USER}:${FS_GROUP} /usr/local/${BACKUP_NAME}


echo -e "${BLUE}Installing ${MODULE_NAME}...${NC}"
# Install from the module directory using targeted install
sudo make ${MODULE_NAME}-install
if [ $? -ne 0 ]; then
    echo -e "${RED}Install failed${NC}"
    exit 1
fi

# Copy configuration file
echo -e "${BLUE}Installing configuration...${NC}"
if [ ! -f "${CONF_PATH}/autoload_configs/parlez.conf.xml" ]; then
    sudo tee "${CONF_PATH}/autoload_configs/parlez.conf.xml" > /dev/null << 'EOF'
<configuration name="parlez.conf" description="Parlez Translation Module Configuration">
  <settings>
    <!-- Azure Cognitive Services configuration -->
    <param name="azure-subscription-key" value="EsWU20063FhzjdZ8t8FSWuGpTIJ24j9UmhK6xtK7cI3eyfD50gA4JQQJ99BEACmepeSXJ3w3AAAYACOGwsdk"/>
    <param name="azure-region" value="uksouth"/>
    
    <!-- Audio processing settings -->
    <param name="buffer-size" value="3200"/>
    <param name="max-silence-ms" value="1000"/>
    
    <!-- Default language mappings -->
    <param name="default-source-language" value="en-US"/>
    <param name="default-target-language" value="fr-FR"/>
    <param name="default-voice" value="fr-FR-DeniseNeural"/>
    
    <!-- Logging settings -->
    <param name="log-level" value="info"/>
    <param name="speech-log-path" value="${LOG_PATH}/speech_sdk.log"/>
  </settings>
</configuration>
EOF
    echo -e "${YELLOW}Created default configuration file. Please edit with your Azure credentials.${NC}"
fi

echo -e "${BLUE}Update log file mappings ...${NC}"
# Update logfile configuration with module's cpp files
"$(dirname "$0")/ps-update-logfile-mappings.sh" -m "$MODULE_NAME" -s "$MODULE_SRC"
if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to update logfile configuration${NC}"
    exit 1
fi


echo -e "${BLUE}Reseting permissions ${FS_USER}:${FS_GROUP} ${FREESWITCH_INSTALL_DIR} ...${NC}"
sudo chown -R ${FS_USER}:${FS_GROUP} ${FREESWITCH_INSTALL_DIR}


echo -e "${BLUE}Restarting freeswitch ...${NC}"
if pgrep -x "freeswitch" > /dev/null; then
   sudo systemctl restart ${SERVICE_NAME}.service
else
    echo -e "${YELLOW}FreeSWITCH is not running. Start it to load the module.${NC}"
fi