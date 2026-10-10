#!/bin/sh
set -eu

DEST_DIR="/etc/udev/rules.d"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(id -u)" -ne 0 ]; then
    echo "error: 需要 root 权限，请用 sudo 运行。" >&2
    exit 1
fi
for RULE_NAME in 99-rk-rockusb-all.rules 99-allwinner.rules; do
    if [ ! -f "$SCRIPT_DIR/$RULE_NAME" ]; then
        echo "error: 找不到 $SCRIPT_DIR/$RULE_NAME" >&2
        exit 1
    fi
done
install -d -m 0755 "$DEST_DIR"
for RULE_NAME in 99-rk-rockusb-all.rules 99-allwinner.rules; do
    install -m 0644 "$SCRIPT_DIR/$RULE_NAME" "$DEST_DIR/$RULE_NAME"
    echo "已安装 $DEST_DIR/$RULE_NAME"
done
udevadm control --reload-rules
udevadm trigger
