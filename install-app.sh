#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APPDIR="$HOME/.local/opt/wintask-linux"
DESKTOPDIR="$HOME/.local/share/applications"
ICONDIR="$HOME/.local/share/icons/hicolor/scalable/apps"
BINDIR="$HOME/.local/bin"
mkdir -p "$APPDIR" "$DESKTOPDIR" "$ICONDIR" "$BINDIR"
install -m 755 "$HERE/taskmgr" "$APPDIR/taskmgr"
ln -sf "$APPDIR/taskmgr" "$BINDIR/taskmgr-linux"
install -m 644 "$HERE/wintask-linux.svg" "$ICONDIR/wintask-linux.svg"
cat > "$DESKTOPDIR/wintask-linux.desktop" <<DESKTOP_EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=Task Manager
GenericName=System Monitor
Comment=Monitor processes, CPU, memory, users and services
Exec=$APPDIR/taskmgr
TryExec=$APPDIR/taskmgr
Icon=wintask-linux
Terminal=false
Categories=System;Monitor;Utility;
StartupNotify=true
StartupWMClass=TaskManager
DESKTOP_EOF
if command -v kbuildsycoca6 >/dev/null 2>&1; then kbuildsycoca6 --noincremental >/dev/null 2>&1 || true; fi
printf 'Installed Task Manager. Open it from the KDE application launcher.\n'
printf 'To pin it: launch it, right-click its taskbar icon, choose Pin to Task Manager.\n'
printf 'To remove it later, run: %s/uninstall-app.sh\n' "$HERE"
