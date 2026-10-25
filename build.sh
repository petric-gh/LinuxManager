set -eu
cd "$(dirname "$0")"
command -v cc >/dev/null 2>&1 || { echo 'Missing C compiler (cc).' >&2; exit 1; }
command -v pkg-config >/dev/null 2>&1 || true
if ! printf '#include <X11/Xlib.h>\nint main(void){return 0;}\n' | cc -x c - -lX11 -l:libXft.so.2 -l:libXrender.so.1 -o /tmp/taskmgr-x11-check-$$ >/dev/null 2>&1; then
  echo 'Missing X11 or Xft runtime/development libraries. Install X11 development files and the Xft runtime package for your distribution.' >&2
  exit 1
fi
rm -f /tmp/taskmgr-x11-check-$$
make clean all size
