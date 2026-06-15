#!/bin/bash
#
# smcfan installer — installs a launchd service that pins the SMC fan speed
# to a fixed RPM. For Macs whose SMC fan auto-control is broken (fans stuck
# at max, or stuck off).
#
# Usage:
#   sudo ./install.sh            # default 2900 rpm
#   sudo ./install.sh 3200       # custom rpm
#
set -euo pipefail

RPM="${1:-2900}"
DIR="$(cd "$(dirname "$0")" && pwd)"
LABEL=com.local.smcfan
PLIST=/Library/LaunchDaemons/$LABEL.plist
BIN=/usr/local/bin/smcfan
ARCH="$(uname -m)"

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

if [[ $EUID -ne 0 ]]; then
    red "Please run as root:  sudo $0 [rpm]"
    exit 1
fi

# --- validate rpm ---
if ! [[ "$RPM" =~ ^[0-9]+$ ]] || (( RPM < 1000 || RPM > 7000 )); then
    red "RPM must be an integer between 1000 and 7000 (got: $RPM)"
    exit 1
fi

bold "smcfan installer — target ${RPM} rpm on ${ARCH}"

# --- obtain the smcfan binary: compile if possible, else use bundled prebuilt ---
mkdir -p /usr/local/bin
if xcrun -f clang >/dev/null 2>&1; then
    echo "compiling from source..."
    clang -O2 -framework IOKit -o "$BIN" "$DIR/smcfan.c"
    green "compiled -> $BIN"
elif [[ -f "$DIR/smcfan-$ARCH" ]]; then
    echo "no compiler found; using bundled prebuilt binary (smcfan-$ARCH)..."
    cp "$DIR/smcfan-$ARCH" "$BIN"
    green "installed prebuilt -> $BIN"
else
    red "No compiler (Xcode Command Line Tools) and no prebuilt binary for $ARCH."
    red "Install the tools with:  xcode-select --install"
    exit 1
fi
chmod 755 "$BIN"

# --- sanity check: can we talk to the SMC and see fans? ---
echo "checking SMC access..."
if ! "$BIN" status >/dev/null 2>&1; then
    red "smcfan could not read the SMC. Aborting before installing the service."
    exit 1
fi
"$BIN" status

# --- warn about competing fan controllers ---
if pgrep -qi "Macs Fan Control" 2>/dev/null || \
   [[ -f /Library/LaunchDaemons/com.crystalidea.macsfancontrol.smcwrite.plist ]]; then
    red "WARNING: Macs Fan Control is present. It will fight smcfan over the"
    red "         fan target. Quit it (and remove its helper) for smcfan to win."
fi

# --- install the launch daemon ---
echo "installing launchd service..."
sed "s/__RPM__/${RPM}/" "$DIR/$LABEL.plist" > "$PLIST"
chown root:wheel "$PLIST"
chmod 644 "$PLIST"

launchctl bootout system/$LABEL 2>/dev/null || true
launchctl bootstrap system "$PLIST"

sleep 3
green "installed. current state:"
"$BIN" status
echo
bold "Done. Logs: /var/log/smcfan.log"
echo "Change speed later:  sudo smcfan set <rpm>"
echo "Restore auto mode:   sudo smcfan auto"
echo "Uninstall:           sudo ./uninstall.sh"
