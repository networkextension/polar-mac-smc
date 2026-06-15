#!/bin/bash
#
# smcfan uninstaller — stops and removes the service and restores the SMC
# to automatic fan control.
#
#   sudo ./uninstall.sh
#
set -euo pipefail

LABEL=com.local.smcfan
PLIST=/Library/LaunchDaemons/$LABEL.plist
BIN=/usr/local/bin/smcfan

if [[ $EUID -ne 0 ]]; then
    echo "Please run as root:  sudo $0"
    exit 1
fi

echo "stopping service..."
launchctl bootout system/$LABEL 2>/dev/null || true
rm -f "$PLIST"

if [[ -x "$BIN" ]]; then
    echo "restoring automatic fan mode..."
    "$BIN" auto || true
    rm -f "$BIN"
fi

rm -f /var/log/smcfan.log
echo "uninstalled. Fans returned to SMC automatic control."
echo "NOTE: if this machine's SMC auto-control is broken, the fans may now"
echo "      run at max — that is the SMC default, not a bug in the uninstall."
