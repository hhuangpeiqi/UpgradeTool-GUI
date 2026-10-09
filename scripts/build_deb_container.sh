#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
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

DEB=$(ls "$BUILD_DIR"/build/*.deb 2>/dev/null | head -1)
if [ -z "$DEB" ]; then
	echo "error: 未生成 .deb。" >&2
	exit 1
fi

echo
echo ">> 产物: $DEB"
echo ">> Depends:"
dpkg-deb -f "$DEB" Depends 2>/dev/null || true
