#!/bin/bash
# Install a BUILT webOS .ipk onto a device via ares-install.
#
# Usage:
#   bash tools/webos/deploy-dev.sh [path-to.ipk]
#
# If no path is given, picks the newest .ipk under dist/ (the output of tools/webos/build.sh).

set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
DIST_DIR="${DIST_DIR:-$PROJ/dist}"

IPK="$1"
if [ -z "$IPK" ]; then
    IPK="$(find "$DIST_DIR" -maxdepth 1 -name 'io.github.wszqkzqk.pvzportable_*_arm.ipk' 2>/dev/null | sort | tail -n1 || true)"
fi

if [ -z "$IPK" ] || [ ! -f "$IPK" ]; then
    echo "ERROR: no .ipk found to deploy." >&2
    echo "Build one first:" >&2
    echo "  bash tools/webos/build.sh" >&2
    exit 1
fi

if ! command -v ares-install >/dev/null 2>&1; then
    echo "ERROR: ares-install not found on PATH (webosbrew/ares-cli-rs)." >&2
    exit 1
fi

echo "Installing $IPK ..."
ares-install "$IPK"
echo "Done."
