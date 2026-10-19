#!/bin/sh
# Build the Debian/Ubuntu .deb.
#
#   scripts/package_deb.sh [BUILD_DIR] [IMAGE]   container build (default, Ubuntu 22.04 baseline)
#   UPGRADE_TOOL_NATIVE_DEB=1 scripts/package_deb.sh [BUILD_DIR]
#                                                native build, artifact matches only the current distribution
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

report() {
	DEB=$(ls -t "$1"/build/*.deb "$1"/*.deb 2>/dev/null | head -1 || true)
	if [ -z "${DEB:-}" ]; then
		echo "error: no .deb was produced." >&2
		exit 1
	fi

	echo
	echo ">> Artifact: $DEB"
	echo ">> Depends:"
	dpkg-deb -f "$DEB" Depends 2>/dev/null || true
}

# Build directly with the host toolchain; fast, but the libc/libstdc++ baseline is the host's version.
build_native() {
	BUILD_DIR=${1:-"$ROOT/build"}
	echo "warning: native build; the resulting .deb only matches the current distribution." >&2
	if ! command -v dpkg-shlibdeps >/dev/null 2>&1; then
		echo "error: dpkg-dev (providing dpkg-shlibdeps) is required; run apt install dpkg-dev." >&2
		exit 1
	fi

	cmake -S "$ROOT" -B "$BUILD_DIR" -DUPGRADE_TOOL_TESTS=ON
	cmake --build "$BUILD_DIR" -j
	ctest --test-dir "$BUILD_DIR" --output-on-failure
	cpack --config "$BUILD_DIR/CPackConfig.cmake" -G DEB -B "$BUILD_DIR"

	report "$BUILD_DIR"
}

# Build inside an Ubuntu 22.04 + Qt 6.2 container; the artifact installs on Ubuntu 22.04 and newer.
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
		echo "error: docker or podman not found." >&2
		exit 1
	fi

	mkdir -p "$BUILD_DIR/build"
	if [ "$(id -u)" = "0" ]; then
		chown -R "$RUN_UID:$RUN_GID" "$BUILD_DIR"
	fi

	echo ">> Building image $IMAGE (Ubuntu 22.04 + Qt 6.2)"
	"$ENGINE" build -t "$IMAGE" -f "$ROOT/scripts/deb/Dockerfile" "$ROOT/scripts/deb"

	echo ">> Compiling and packaging inside the container"
	"$ENGINE" run --rm \
		--user "$RUN_UID:$RUN_GID" \
		-e HOME=/tmp \
		-v "$ROOT":/src:ro \
		-v "$BUILD_DIR":/out \
		"$IMAGE" \
		sh -c '
			set -eu
			cmake -S /src -B /out/build \
				-DCMAKE_BUILD_TYPE=Release \
				-DUPGRADE_TOOL_TESTS=ON
			cmake --build /out/build -j"$(nproc)"
			ctest --test-dir /out/build --output-on-failure
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
