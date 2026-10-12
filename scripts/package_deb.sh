#!/bin/sh
# 构建 Debian/Ubuntu 的 .deb。
#
#   scripts/package_deb.sh [BUILD_DIR] [IMAGE]   容器构建（默认，基线 Ubuntu 22.04）
#   UPGRADE_TOOL_NATIVE_DEB=1 scripts/package_deb.sh [BUILD_DIR]
#                                                本机构建，产物只适用于当前发行版
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

report() {
	DEB=$(ls -t "$1"/build/*.deb "$1"/*.deb 2>/dev/null | head -1 || true)
	if [ -z "${DEB:-}" ]; then
		echo "error: 未生成 .deb。" >&2
		exit 1
	fi

	echo
	echo ">> 产物: $DEB"
	echo ">> Depends:"
	dpkg-deb -f "$DEB" Depends 2>/dev/null || true
}

# 宿主机工具链直接构建，快捷但产物的 libc/libstdc++ 基线即宿主机版本。
build_native() {
	BUILD_DIR=${1:-"$ROOT/build"}
	echo "warning: 本机构建，产出的 .deb 只适用于当前发行版。" >&2
	if ! command -v dpkg-shlibdeps >/dev/null 2>&1; then
		echo "error: 需要 dpkg-dev（提供 dpkg-shlibdeps），请先 apt install dpkg-dev。" >&2
		exit 1
	fi

	cmake -S "$ROOT" -B "$BUILD_DIR"
	cmake --build "$BUILD_DIR" -j
	cpack --config "$BUILD_DIR/CPackConfig.cmake" -G DEB -B "$BUILD_DIR"

	report "$BUILD_DIR"
}

# 在 Ubuntu 22.04 + Qt 6.2 容器内构建，产物可安装在 Ubuntu 22.04 及更新的版本上。
build_container() {
	BUILD_DIR=${1:-"$ROOT/build-deb"}
	IMAGE=${2:-upgrade-tool-deb-builder:22.04}

	RUN_UID=${SUDO_UID:-$(id -u)}
	RUN_GID=${SUDO_GID:-$(id -g)}

	ENGINE=""
	if command -v docker >/dev/null 2>&1; then
		ENGINE=docker
	elif command -v podman >/dev/null 2>&1; then
		ENGINE=podman
	else
		echo "error: 未找到 docker 或 podman。" >&2
		exit 1
	fi

	mkdir -p "$BUILD_DIR/build"
	if [ "$(id -u)" = "0" ]; then
		chown -R "$RUN_UID:$RUN_GID" "$BUILD_DIR"
	fi

	echo ">> 构建镜像 $IMAGE（Ubuntu 22.04 + Qt 6.2）"
	"$ENGINE" build -t "$IMAGE" -f "$ROOT/scripts/deb/Dockerfile" "$ROOT/scripts/deb"

	echo ">> 容器内编译并打包"
	"$ENGINE" run --rm \
		--user "$RUN_UID:$RUN_GID" \
		-e HOME=/tmp \
		-v "$ROOT":/src:ro \
		-v "$BUILD_DIR":/out \
		"$IMAGE" \
		sh -c '
			set -eu
			cmake -S /src -B /out/build \
				-DCMAKE_BUILD_TYPE=Release
			cmake --build /out/build -j"$(nproc)"
			cpack --config /out/build/CPackConfig.cmake -G DEB -B /out/build
		'

	if [ "$(id -u)" = "0" ]; then
		chown -R "$RUN_UID:$RUN_GID" "$BUILD_DIR"
	fi

	report "$BUILD_DIR"
}

if [ "${UPGRADE_TOOL_NATIVE_DEB:-0}" = "1" ]; then
	build_native "$@"
else
	build_container "$@"
fi
