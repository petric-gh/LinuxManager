#!/bin/sh
set -eu
APPDIR="$HOME/.local/opt/wintask-linux"
BINDIR="$HOME/.local/bin"
DESKTOPDIR="$HOME/.local/share/applications"
ICONDIR="$HOME/.local/share/icons/hicolor/scalable/apps"
rm -f "$APPDIR/taskmgr" "$DESKTOPDIR/wintask-linux.desktop" "$BINDIR/taskmgr-linux"
rm -f "$ICONDIR/wintask-linux.svg"
rmdir "$APPDIR" 2>/dev/null || true
if command -v kbuildsycoca6 >/dev/null 2>&1; then kbuildsycoca6 --noincremental >/dev/null 2>&1 || true; fi
printf 'Task Manager has been uninstalled.\n'
printf 'Your personal files and system services were not changed.\n'
