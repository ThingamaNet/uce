#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEB_ASSET_DIR="$SCRIPT_DIR/deb"
PACKAGE_NAME="uce"
REVISION="${UCE_DEB_REVISION:-1}"

usage() {
	cat <<'EOF'
Usage:
  scripts/make_deb.sh VERSION

Environment:
  UCE_DEB_REVISION   Debian package revision suffix (default: 1)
  UCE_DEB_ARCH       Override package architecture
EOF
}

require_command() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Required command not found: $1" >&2
		exit 1
	fi
}

resolve_arch() {
	if [[ -n "${UCE_DEB_ARCH:-}" ]]; then
		printf '%s\n' "$UCE_DEB_ARCH"
		return
	fi
	if command -v dpkg-architecture >/dev/null 2>&1; then
		dpkg-architecture -qDEB_HOST_ARCH
		return
	fi
	dpkg --print-architecture
}

validate_version() {
	local version="$1"
	if [[ ! "$version" =~ ^[0-9][A-Za-z0-9.+:~]*$ ]]; then
		echo "Invalid Debian version string: $version" >&2
		exit 1
	fi
}

copy_payload() {
	local destination="$1"
	local path
	for path in LICENSE README.md codesearch bin scripts site src; do
		cp -a "$REPO_ROOT/$path" "$destination/"
	done
	mkdir -p "$destination/etc"
	cp -a "$REPO_ROOT/etc/uce" "$destination/etc/"
}

write_control_file() {
	local output_file="$1"
	local package_version="$2"
	local arch="$3"
	local installed_size="$4"

	sed \
		-e "s/@PACKAGE_NAME@/$PACKAGE_NAME/g" \
		-e "s/@VERSION@/$package_version/g" \
		-e "s/@ARCH@/$arch/g" \
		-e "s/@INSTALLED_SIZE@/$installed_size/g" \
		"$DEB_ASSET_DIR/control.in" > "$output_file"
}

write_md5sums() {
	local stage_dir="$1"
	(
		cd "$stage_dir"
		find usr etc lib -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums
	)
}

if [[ $# -ne 1 ]]; then
	usage >&2
	exit 1
fi

VERSION="$1"
validate_version "$VERSION"

require_command bash
require_command clang++
require_command dpkg-deb
require_command dpkg
require_command install
require_command find
require_command xargs
require_command md5sum
require_command mysql_config
require_command du
require_command sed
require_command awk
require_command cp
require_command sort

ARCH="$(resolve_arch)"
PACKAGE_VERSION="${VERSION}-${REVISION}"
PACKAGE_BASENAME="${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCH}"
STAGE_DIR="$REPO_ROOT/pkg/$PACKAGE_BASENAME"
DEBIAN_DIR="$STAGE_DIR/DEBIAN"
INSTALL_ROOT="$STAGE_DIR/usr/lib/uce"
DIST_DIR="$REPO_ROOT/dist"
OUTPUT_DEB="$DIST_DIR/$PACKAGE_BASENAME.deb"

echo "Making package $PACKAGE_BASENAME"
echo "==================================="

bash "$REPO_ROOT/scripts/build_linux.sh"

rm -rf -- "$STAGE_DIR"
mkdir -p "$DEBIAN_DIR" "$INSTALL_ROOT" "$STAGE_DIR/etc/uce" "$STAGE_DIR/lib/systemd/system" "$DIST_DIR"

copy_payload "$INSTALL_ROOT"

install -m 0644 "$REPO_ROOT/etc/uce/settings.cfg" "$STAGE_DIR/etc/uce/settings.cfg"
install -m 0644 "$DEB_ASSET_DIR/uce.service" "$STAGE_DIR/lib/systemd/system/uce.service"
install -m 0644 "$DEB_ASSET_DIR/conffiles" "$DEBIAN_DIR/conffiles"
install -m 0755 "$DEB_ASSET_DIR/postinst" "$DEBIAN_DIR/postinst"
install -m 0755 "$DEB_ASSET_DIR/prerm" "$DEBIAN_DIR/prerm"
install -m 0755 "$DEB_ASSET_DIR/postrm" "$DEBIAN_DIR/postrm"

INSTALLED_SIZE="$(du -sk "$STAGE_DIR" | awk '{print $1}')"
write_control_file "$DEBIAN_DIR/control" "$PACKAGE_VERSION" "$ARCH" "$INSTALLED_SIZE"
write_md5sums "$STAGE_DIR"

dpkg-deb --root-owner-group --build "$STAGE_DIR" "$OUTPUT_DEB"

dpkg-deb -I "$OUTPUT_DEB" | sed -n '1,80p'
echo
dpkg-deb -c "$OUTPUT_DEB" | sed -n '1,60p'
