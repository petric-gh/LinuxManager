set -Eeuo pipefail

REPO="https://github.com/petric-gh/LinuxManager"
BRANCH="main"
APPDIR="${HOME}/.local/opt/wintask-linux"
INSTALLED="${APPDIR}/taskmgr"

if [[ ! -f "$INSTALLED" || ! -x "$INSTALLED" ]]; then
    printf 'LinuxManager does not appear to be installed at:\n  %s\n' "$INSTALLED" >&2
    printf 'Nothing was changed. Install the app first, then run this updater again.\n' >&2
    exit 1
fi

for cmd in curl tar make cc install mv mktemp; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        printf 'Missing required command: %s\n' "$cmd" >&2
        exit 1
    fi
done

TMPDIR_UPDATE="$(mktemp -d)"
APP_TEMP="${APPDIR}/.taskmgr.update.$$"
cleanup() {
    rm -rf -- "$TMPDIR_UPDATE"
    rm -f -- "$APP_TEMP"
}
trap cleanup EXIT

ARCHIVE="${TMPDIR_UPDATE}/source.tar.gz"
EXTRACT_DIR="${TMPDIR_UPDATE}/source"
NEW_BINARY="${TMPDIR_UPDATE}/taskmgr.new"

printf 'Downloading the latest LinuxManager source from GitHub...\n'
curl --fail --location --silent --show-error \
    "${REPO}/archive/refs/heads/${BRANCH}.tar.gz" \
    --output "$ARCHIVE"

mkdir -p "$EXTRACT_DIR"
tar -xzf "$ARCHIVE" -C "$EXTRACT_DIR" --strip-components=1

if [[ ! -f "${EXTRACT_DIR}/Makefile" || ! -f "${EXTRACT_DIR}/taskmgr.c" ]]; then
    printf 'The downloaded GitHub source did not contain the expected files.\n' >&2
    exit 1
fi

printf 'Building in a temporary directory (your project/build executable will not be touched)...\n'
make -C "$EXTRACT_DIR" taskmgr

if [[ ! -f "${EXTRACT_DIR}/taskmgr" || ! -x "${EXTRACT_DIR}/taskmgr" ]]; then
    printf 'Build failed: no executable was produced.\n' >&2
    exit 1
fi

install -m 755 "${EXTRACT_DIR}/taskmgr" "$NEW_BINARY"
install -m 755 "$NEW_BINARY" "$APP_TEMP"
mv -f -- "$APP_TEMP" "$INSTALLED"

printf '\nLinuxManager was updated successfully.\n'
printf 'Updated installed executable: %s\n' "$INSTALLED"
