#!/usr/bin/env bash
# Refresh the vendored libBrightLink tree from upstream.
#
# bright-iputils ships libbrightlink at subprojects/libbrightlink/ as
# vendored source (not a git submodule) so Launchpad recipe builds work:
# git-build-recipe does not run `git submodule update`.
#
# Usage:
#   ./tools/update-libbrightlink.sh [commit-ish]
#
# Default commit-ish is the SHA recorded in subprojects/libbrightlink/VENDOR_REVISION.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VENDOR_DIR="${REPO_ROOT}/subprojects/libbrightlink"
UPSTREAM_URL="${LIBBRIGHTLINK_URL:-https://github.com/Digital-Defiance/libbrightlink.git}"
REVISION_FILE="${VENDOR_DIR}/VENDOR_REVISION"

if [ ! -f "${REVISION_FILE}" ]; then
	echo "ERROR: ${REVISION_FILE} is missing." >&2
	exit 1
fi

DEFAULT_REV="$(tr -d '[:space:]' < "${REVISION_FILE}")"
TARGET_REV="${1:-${DEFAULT_REV}}"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "${TMPDIR}"' EXIT

echo "==> Fetching libbrightlink ${TARGET_REV} from ${UPSTREAM_URL}"
git -C "${TMPDIR}" init -q
git -C "${TMPDIR}" remote add origin "${UPSTREAM_URL}"
git -C "${TMPDIR}" fetch -q --depth 1 origin "${TARGET_REV}"
git -C "${TMPDIR}" checkout -q FETCH_HEAD

echo "==> Replacing ${VENDOR_DIR}"
find "${VENDOR_DIR}" -mindepth 1 -maxdepth 1 ! -name VENDOR_REVISION -exec rm -rf {} +
rsync -a --delete \
	--exclude build/ --exclude build-*/ --exclude .git/ \
	"${TMPDIR}/" "${VENDOR_DIR}/"

printf '%s\n' "${TARGET_REV}" > "${REVISION_FILE}"

echo "==> Vendored libbrightlink at ${TARGET_REV}"
echo "    Review and commit: git add subprojects/libbrightlink && git commit"
