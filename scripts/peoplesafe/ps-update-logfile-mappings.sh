#!/bin/bash

# update-logfile-mappings.sh
# Script to automatically update logfile.conf.xml with module's cpp files

# Source common paths if available
if [ -f "$(dirname "$0")/ps-constants.sh" ]; then
    source "$(dirname "$0")/ps-constants.sh"
fi

# Define colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

show_usage() {
    echo "Usage: $0 -m <module_name> -s <module_source_path> [-c <config_path>] [-p <profile_name>]"
    echo ""
    echo "Options:"
    echo "  -m  Module name (required)"
    echo "  -s  Module source path (required)"
    echo "  -c  FreeSWITCH config path (optional, defaults to \$CONF_PATH or /usr/local/freeswitch/conf)"
    echo "  -p  Profile name in logfile.conf.xml (optional, defaults to module name)"
    echo "  -h  Show this help"
    echo ""
    echo "Example:"
    echo "  $0 -m parlez -s /usr/src/freeswitch/src/mod/applications/mod_parlez"
}

# Initialize variables
MODULE_NAME=""
MODULE_SRC=""
CONFIG_PATH=""
PROFILE_NAME=""

# Parse command line options
while getopts "m:s:c:p:h" opt; do
    case $opt in
        m)
            MODULE_NAME="$OPTARG"
            ;;
        s)
            MODULE_SRC="$OPTARG"
            ;;
        c)
            CONFIG_PATH="$OPTARG"
            ;;
        p)
            PROFILE_NAME="$OPTARG"
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

# Validate required parameters
if [ -z "$MODULE_NAME" ] || [ -z "$MODULE_SRC" ]; then
    echo -e "${RED}Error: Module name and source path are required${NC}"
    show_usage
    exit 1
fi

# Set defaults
if [ -z "$CONFIG_PATH" ]; then
    if [ -n "$CONF_PATH" ]; then
        CONFIG_PATH="$CONF_PATH"
    else
        CONFIG_PATH="/usr/local/freeswitch/conf"
    fi
fi

if [ -z "$PROFILE_NAME" ]; then
    PROFILE_NAME="$MODULE_NAME"
fi

# Validate paths
if [ ! -d "$MODULE_SRC" ]; then
    echo -e "${RED}Error: Module source directory does not exist: ${MODULE_SRC}${NC}"
    exit 1
fi

if [ ! -d "$CONFIG_PATH" ]; then
    echo -e "${RED}Error: Config directory does not exist: ${CONFIG_PATH}${NC}"
    exit 1
fi

LOGFILE_CONF="${CONFIG_PATH}/autoload_configs/logfile.conf.xml"

if [ ! -f "$LOGFILE_CONF" ]; then
    echo -e "${RED}Error: logfile.conf.xml not found at ${LOGFILE_CONF}${NC}"
    exit 1
fi

# Main function to update logfile configuration
update_logfile_config() {
    echo -e "${BLUE}Updating logfile.conf.xml for ${MODULE_NAME} (profile: ${PROFILE_NAME})...${NC}"
    
    # # Create backup of logfile.conf.xml
    # BACKUP_FILE="${LOGFILE_CONF}.bak.$(date +%Y%m%d-%H%M%S)"
    # sudo cp "$LOGFILE_CONF" "$BACKUP_FILE"
    # echo -e "${CYAN}Created backup: ${BACKUP_FILE}${NC}"
    
    # Find all .cpp files in the module source directory
    CPP_FILES=$(find "$MODULE_SRC" -name "*.cpp" -exec basename {} \; | sort)
    
    if [ -z "$CPP_FILES" ]; then
        echo -e "${YELLOW}No .cpp files found in ${MODULE_SRC}${NC}"
        return 0
    fi
    
    # echo -e "${CYAN}Found .cpp files:${NC}"
    # for file in $CPP_FILES; do
    #     echo -e "${CYAN}  - ${file}${NC}"
    # done
    
    # Create temporary file for the new mappings
    TEMP_MAPPINGS=$(mktemp)
    
    # Generate new mappings for each cpp file
    echo "				<!-- Auto-generated mappings for ${MODULE_NAME} - $(date) -->" > "$TEMP_MAPPINGS"
    for cpp_file in $CPP_FILES; do
        echo "				<map name=\"${cpp_file}\" value=\"console,debug,info,notice,warning,err,crit\"/>" >> "$TEMP_MAPPINGS"
    done
    echo "				<!-- End auto-generated mappings for ${MODULE_NAME} -->" >> "$TEMP_MAPPINGS"
    
    # Create a temporary file for the updated XML
    TEMP_XML=$(mktemp)
    
    # Check if the profile exists
    if ! grep -q "<profile name=\"${PROFILE_NAME}\">" "$LOGFILE_CONF"; then
        echo -e "${RED}Error: Profile '${PROFILE_NAME}' not found in logfile.conf.xml${NC}"
        echo -e "${YELLOW}Available profiles:${NC}"
        grep "<profile name=" "$LOGFILE_CONF" | sed 's/.*name="\([^"]*\)".*/  - \1/'
        rm -f "$TEMP_MAPPINGS" "$TEMP_XML"
        return 1
    fi
    
    # Use awk to replace the mappings section in the specified profile
    awk -v profile_name="$PROFILE_NAME" -v mappings_file="$TEMP_MAPPINGS" '
    BEGIN { 
        in_target_profile = 0; 
        in_mappings = 0; 
        profile_pattern = "<profile name=\"" profile_name "\">";
    }
    
    # Detect start of target profile
    $0 ~ profile_pattern { 
        in_target_profile = 1; 
        print; 
        next 
    }
    
    # Detect end of any profile
    /<\/profile>/ { 
        if (in_target_profile) {
            in_target_profile = 0; 
            in_mappings = 0;
        }
        print; 
        next 
    }
    
    # If we are in target profile, look for mappings
    in_target_profile && /<mappings>/ { 
        in_mappings = 1; 
        print;
        # Insert our new mappings here
        while ((getline line < mappings_file) > 0) {
            print line
        }
        close(mappings_file)
        next 
    }
    
    # End of mappings in target profile
    in_target_profile && in_mappings && /<\/mappings>/ { 
        in_mappings = 0; 
        print; 
        next 
    }
    
    # Skip lines inside mappings section of target profile only
    in_target_profile && in_mappings { next }
    
    # Print all other lines
    { print }
    ' "$LOGFILE_CONF" > "$TEMP_XML"
    
    # Validate the XML is still well-formed (basic check)
    if ! grep -q "</configuration>" "$TEMP_XML"; then
        echo -e "${RED}Error: Generated XML appears to be malformed${NC}"
        rm -f "$TEMP_MAPPINGS" "$TEMP_XML"
        return 1
    fi
    
    # Replace the original file
    sudo cp "$TEMP_XML" "$LOGFILE_CONF"
    
    # Clean up temporary files
    rm -f "$TEMP_MAPPINGS" "$TEMP_XML"
    
    echo -e "${GREEN}Successfully updated logfile.conf.xml with ${MODULE_NAME} mappings${NC}"
    
    # Show what was added
    # echo -e "${CYAN}Added mappings:${NC}"
    # for cpp_file in $CPP_FILES; do
    #     echo -e "${CYAN}  ${cpp_file} -> console,debug,info,notice,warning,err,crit${NC}"
    # done
    
    return 0
}

# Run the update
update_logfile_config

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Logfile configuration update completed successfully${NC}"
    exit 0
else
    echo -e "${RED}Logfile configuration update failed${NC}"
    exit 1
fi