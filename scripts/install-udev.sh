#!/bin/sh
set -eu

RULE_NAME="99-rk-rockusb-all.rules"
DEST_DIR="/etc/udev/rules.d"

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SRC="$SCRIPT_DIR/$RULE_NAME"

if [ "$(id -u)" -ne 0 ]; then
	echo "error: 需要 root 权限，请用 sudo 运行。" >&2
	exit 1
fi

if [ ! -f "$SRC" ]; then
	echo "error: 找不到 $SRC" >&2
	exit 1
fi

install -m 0644 "$SRC" "$DEST_DIR/$RULE_NAME"
udevadm control --reload-rules
udevadm trigger

echo "已安装 $DEST_DIR/$RULE_NAME"
