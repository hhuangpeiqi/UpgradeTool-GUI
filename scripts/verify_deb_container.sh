#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ "$#" -ge 1 ] && [ -f "$1" ]; then
	DEB=$1
	shift
else
	DEB=$(ls -t "$ROOT"/build-deb/build/*.deb "$ROOT"/build/*.deb 2>/dev/null | head -1 || true)
fi
if [ -z "${DEB:-}" ] || [ ! -f "$DEB" ]; then
	echo "error: no .deb found; run scripts/package_deb.sh first." >&2
	exit 1
fi
DEB=$(CDPATH= cd -- "$(dirname -- "$DEB")" && pwd)/$(basename -- "$DEB")

if dpkg-deb -f "$DEB" Depends 2>/dev/null | grep -qE "libzip|libxml2"; then
	echo "error: $DEB depends on a distribution library (libzip, libxml2) that must be linked statically." >&2
	exit 1
fi

if [ "$#" -ge 1 ]; then
	RELEASES=$*
else
	RELEASES="22.04 24.04 26.04"
fi

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

DEB_DIR=$(dirname -- "$DEB")
DEB_NAME=$(basename -- "$DEB")
STATUS=0

for REL in $RELEASES; do
	echo
	echo "==================================================================="
	echo ">> Verifying ubuntu:$REL"
	echo "==================================================================="
	if "$ENGINE" run --rm \
		-e DEB_NAME="$DEB_NAME" \
		-v "$DEB_DIR":/pkg:ro \
		"ubuntu:$REL" \
		sh -c '
			set -eu
			export DEBIAN_FRONTEND=noninteractive
			apt-get update -qq
			apt-get install -y -qq --no-install-recommends "/pkg/$DEB_NAME"
			apt-get install -y -qq --no-install-recommends xvfb
			echo "--- GUI render (xvfb + xcb)"
			timeout -k 5 90 xvfb-run -a -s "-screen 0 1280x800x24" \
				env QT_QPA_PLATFORM=xcb \
				upgrade_tool_gui --screenshot /tmp/shot.png
			test -s /tmp/shot.png
			ls -l /tmp/shot.png
			echo "OK"
		'; then
		echo ">> ubuntu:$REL: PASS"
	else
		echo ">> ubuntu:$REL: FAIL" >&2
		STATUS=1
	fi
done

exit $STATUS
