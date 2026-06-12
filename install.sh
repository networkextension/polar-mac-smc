#!/bin/bash
# Install the smcfan fixed-fan-speed service.
# Usage: sudo ./install.sh [rpm]   (default 2900)
set -euo pipefail

RPM="${1:-2900}"
DIR="$(cd "$(dirname "$0")" && pwd)"
LABEL=com.local.smcfan
PLIST=/Library/LaunchDaemons/$LABEL.plist

if [[ $EUID -ne 0 ]]; then
    echo "run as root: sudo $0 [rpm]" >&2
    exit 1
fi

echo "building smcfan..."
mkdir -p /usr/local/bin
clang -O2 -framework IOKit -o /usr/local/bin/smcfan "$DIR/smcfan.c"

echo "installing daemon (target ${RPM} rpm)..."
sed "s/<string>2900<\/string>/<string>${RPM}<\/string>/" \
    "$DIR/$LABEL.plist" > "$PLIST"
chown root:wheel "$PLIST"
chmod 644 "$PLIST"

launchctl bootout system/$LABEL 2>/dev/null || true
launchctl bootstrap system "$PLIST"

echo "installed. current state:"
/usr/local/bin/smcfan status
