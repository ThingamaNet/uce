#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID:-0} -ne 0 ]]; then
	echo "This script must run as root." >&2
	exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
UNIT_NAME="uce.service"
UNIT_SOURCE="$SCRIPT_DIR/uce.service"
UNIT_DEST="/etc/systemd/system/$UNIT_NAME"
CONFIG_SOURCE="$REPO_ROOT/etc/uce/settings.cfg"
CONFIG_DEST="/etc/uce/settings.cfg"

action="${1:-setup}"

install_unit() {
	install -D -m 0644 "$UNIT_SOURCE" "$UNIT_DEST"
	if [[ ! -f "$CONFIG_DEST" ]]; then
		install -D -m 0644 "$CONFIG_SOURCE" "$CONFIG_DEST"
	fi
	systemctl daemon-reload
}

case "$action" in
	install)
		install_unit
		;;
	setup)
		install_unit
		systemctl enable --now "$UNIT_NAME"
		;;
	enable)
		install_unit
		systemctl enable "$UNIT_NAME"
		;;
	start|stop|restart|status)
		systemctl "$action" "$UNIT_NAME"
		;;
	logs)
		lines="${2:-100}"
		journalctl -u "$UNIT_NAME" -n "$lines" --no-pager
		;;
	*)
		echo "Usage: $0 [install|setup|enable|start|stop|restart|status|logs [lines]]" >&2
		exit 1
		;;
esac
