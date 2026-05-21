#!/usr/bin/env bash
# build-ppa.sh — Build and upload a source package to a Launchpad PPA
# Usage: ./build-ppa.sh [ubuntu-series] [launchpad-username]
#   ubuntu-series: jammy (22.04), noble (24.04), oracular (24.10), etc.
#   launchpad-username: your Launchpad account name
#
# Prerequisites (install on Ubuntu/Debian):
#   sudo apt install devscripts debhelper dput meson ninja-build \
#                    libcap-dev libgnutls28-dev libidn2-dev pkg-config

set -euo pipefail

SERIES="${1:-noble}"
LAUNCHPAD_USER="${2:-}"

PACKAGE="bsh-iputils"
UPSTREAM_VERSION="20260520"
DEBIAN_REVISION="1"
PPA_VERSION="${UPSTREAM_VERSION}-${DEBIAN_REVISION}~${SERIES}1"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

echo "==> Building source package ${PACKAGE} ${PPA_VERSION} for ${SERIES}"

# Temporarily rewrite changelog for the target series
CHANGELOG="${REPO_ROOT}/debian/changelog"
ORIG_CHANGELOG=$(cat "$CHANGELOG")

# Update the suite name and version in changelog for this series
sed -i "1s/.*/${PACKAGE} (${PPA_VERSION}) ${SERIES}; urgency=medium/" "$CHANGELOG"

# Build the unsigned source package
(cd "$REPO_ROOT" && dpkg-buildpackage -S -sa -d --no-sign)

# Restore original changelog
echo "$ORIG_CHANGELOG" > "$CHANGELOG"

CHANGES_FILE="$(dirname "$REPO_ROOT")/${PACKAGE}_${PPA_VERSION}_source.changes"

echo "==> Source package built: ${CHANGES_FILE}"

# Sign the .changes file (requires your GPG key registered on Launchpad)
if command -v debsign &>/dev/null; then
    debsign "$CHANGES_FILE"
else
    echo "WARNING: debsign not found. Sign manually with:"
    echo "  debsign ${CHANGES_FILE}"
fi

# Upload to PPA
if [[ -n "$LAUNCHPAD_USER" ]]; then
    echo "==> Uploading to ppa:${LAUNCHPAD_USER}/${PACKAGE} ..."
    dput "ppa:${LAUNCHPAD_USER}/${PACKAGE}" "$CHANGES_FILE"
else
    echo ""
    echo "==> To upload to your PPA, run:"
    echo "  dput ppa:<your-launchpad-username>/${PACKAGE} ${CHANGES_FILE}"
    echo ""
    echo "  Or with the dput.cf config:"
    echo "  dput -c debian/dput.cf bsh-iputils-ppa ${CHANGES_FILE}"
fi
