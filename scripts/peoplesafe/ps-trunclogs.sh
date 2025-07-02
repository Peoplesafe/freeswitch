#!/bin/bash

# This is for debugging parlez logs
# It truncates the parlez and freeswitch logs to only include entries after the last conference

# Source common paths
source "$(dirname "$0")/ps-constants.sh"

# Find the line number of the last occurrence of the conference-create event
LAST_LINE=$(grep -n "mod_parlez\.cpp:.* PARLEZ: Received Conference Event - Action 'conference-create'" "${LOG_PATH}/parlez.log" | tail -1 | cut -d: -f1)

# Extract all lines after the last occurrence
if [ -n "$LAST_LINE" ]; then
    NEXT_LINE=$((LAST_LINE))
    sudo tail -n +$NEXT_LINE "${LOG_PATH}/parlez.log" >/tmp/temp-parlez.log
else
    echo "Error: Could not find 'mod_parlez.cpp:* PARLEZ: Received Conference Event - Action 'conference-create'' in parlez log file" >&2
    exit 1
fi

# Remove debug messages from the temporary file
# These are obsolete becuase messages have changed but here for reference in case we need to add more
# change Text1, Text2, Text3 to the actual text you want to remove
sed -i '/\[DEBUG\] \(translation_session\|mod_parlez\)\.cpp.*\(Text1\|Text2\|Text3\)/d' /tmp/temp-parlez.log 
sudo mv /tmp/temp-parlez.log "${LOG_PATH}/parlez.trunc.log"


# Find the line number of the last occurrence of the conference-create event
LAST_LINE=$(grep -n "mod_parlez\.cpp:.* PARLEZ: Received Conference Event - Action 'conference-create'" "${LOG_PATH}/freeswitch.log" | tail -1 | cut -d: -f1)

# Extract all lines after the last occurrence
if [ -n "$LAST_LINE" ]; then
    NEXT_LINE=$((LAST_LINE))
    sudo tail -n +$NEXT_LINE "${LOG_PATH}/freeswitch.log" >/tmp/temp-freeswitch.log
else
    echo "Error: Could not find 'mod_parlez.cpp:* PARLEZ: Received Conference Event - Action 'conference-create'' in freeswitch log file" >&2
    exit 1
fi

# Truncate the original log file from the found line
sudo tail -n +$NEXT_LINE "${LOG_PATH}/freeswitch.log" > /tmp/temp-freeswitch-orig.log
sudo mv /tmp/temp-freeswitch-orig.log "${LOG_PATH}/freeswitch.log"
sudo chown -R ${FS_USER}:${FS_GROUP} "${LOG_PATH}/"