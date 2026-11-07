#!/usr/bin/env bash
#
# Upload the packaged ezRemote Client payload to the PS5 over FTP, recreating
# the directory tree (assets/, sce_sys/, ...) with --ftp-create-dirs.
#
# Usage: upload.sh [appdir] [ftp_host] [ftp_port] [ftp_dir]
#
# The remote path is percent-encoded before being placed in the URL because curl
# rejects a URL containing raw characters such as spaces (exit 3, "URL rejected:
# Malformed input to a URL function"). The package contains files like
# "assets/langs/Simplified Chinese.ini". The local path handed to -T is kept
# undecoded, since curl turns the escapes back into the original name.
#
# Build with: make upload

export LC_ALL=C

APPDIR="${1:-ezremote-client}"
FTP_HOST="${2:-192.168.100.116}"
FTP_PORT="${3:-2121}"
FTP_DIR="${4:-/data/homebrew/ezremote-client}"

urlencode() {
    local s="$1" out="" c i
    for (( i = 0; i < ${#s}; i++ )); do
        c=${s:i:1}
        case "$c" in
            [A-Za-z0-9.~_/-]) out+="$c" ;;
            *) printf -v c '%%%02X' "'$c"; out+="$c" ;;
        esac
    done
    printf '%s' "$out"
}

cd "$APPDIR" || exit 1

status=0
while IFS= read -r f; do
    remote=$(urlencode "$f")
    if ! curl -sS --fail --ftp-create-dirs -T "$f" "ftp://${FTP_HOST}:${FTP_PORT}${FTP_DIR}/${remote}"; then
        echo "Failed to upload: $f" >&2
        status=1
        break
    fi
done < <(find . -type f | sed 's|^\./||')

exit $status
