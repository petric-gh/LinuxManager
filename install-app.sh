set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APPDIR="$HOME/.local/opt/wintask-linux"
DESKTOPDIR="$HOME/.local/share/applications"
ICONDIR="$HOME/.local/share/icons/hicolor/scalable/apps"
BINDIR="$HOME/.local/bin"
mkdir -p "$APPDIR" "$DESKTOPDIR" "$ICONDIR" "$BINDIR"
install -m 755 "$HERE/taskmgr" "$APPDIR/taskmgr"
ln -sf "$APPDIR/taskmgr" "$BINDIR/taskmgr-linux"
cat > "$DESKTOPDIR/wintask-linux.desktop" <<DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=LinuxManager
GenericName=Task Manager
Comment=The Easiest Task Manager for Linux
Exec=$APPDIR/taskmgr
TryExec=$APPDIR/taskmgr
Terminal=false
Categories=System;Monitor;Utility;
StartupNotify=true
StartupWMClass=TaskManager
DESKTOP_EOF
if command -v kbuildsycoca6 >/dev/null 2>&1; then kbuildsycoca6 --noincremental >/dev/null 2>&1 || true; fi
printf 'Installed Task Manager. Open it from the KDE application launcher.\n'
printf 'To pin it: launch it, right-click its taskbar icon, choose Pin to Task Manager.\n'
printf 'To remove it later, run: %s/uninstall-app.sh\n' "$HERE"
