#!/bin/sh
set -eu

DEST_DIR="/etc/udev/rules.d"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(id -u)" -ne 0 ]; then
    echo "error: root privileges required; run with sudo." >&2
    exit 1
fi
for RULE_NAME in 99-rk-rockusb-all.rules 99-allwinner.rules 99-qualcomm.rules; do
    if [ ! -f "$SCRIPT_DIR/$RULE_NAME" ]; then
        echo "error: $SCRIPT_DIR/$RULE_NAME not found" >&2
        exit 1
    fi
done
install -d -m 0755 "$DEST_DIR"
for RULE_NAME in 99-rk-rockusb-all.rules 99-allwinner.rules 99-qualcomm.rules; do
    install -m 0644 "$SCRIPT_DIR/$RULE_NAME" "$DEST_DIR/$RULE_NAME"
    echo "installed $DEST_DIR/$RULE_NAME"
done
udevadm control --reload-rules
udevadm trigger
