#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ "${UPGRADE_TOOL_NATIVE_DEB:-0}" = "1" ]; then
	echo "warning: 本机构建，产出的 .deb 只适用于当前发行版。" >&2
	BUILD_DIR=${1:-"$ROOT/build"}
	if ! command -v dpkg-shlibdeps >/dev/null 2>&1; then
		echo "error: 需要 dpkg-dev（提供 dpkg-shlibdeps），请先 apt install dpkg-dev。" >&2
		exit 1
	fi
	cmake -S "$ROOT" -B "$BUILD_DIR"
	cmake --build "$BUILD_DIR" -j
	cpack --config "$BUILD_DIR/CPackConfig.cmake" -G DEB -B "$BUILD_DIR"
	exit 0
fi

exec "$ROOT/scripts/build_deb_container.sh" "$@"
