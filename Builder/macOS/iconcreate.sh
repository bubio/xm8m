#!/bin/zsh
set -euo pipefail

if (( $# < 1 || $# > 2 )); then
    print -u2 "Usage: $0 source.png [output.icns]"
    exit 1
fi

SOURCE_IMAGE=$1
OUTPUT_ICON=${2:-AppIcon.icns}
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/xm8-icon.XXXXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT
APP_ICONSET="$WORK_DIR/AppIcon.iconset"
mkdir "$APP_ICONSET"

for size in 16 32 128 256 512; do
    sips -s format png -z "$size" "$size" "$SOURCE_IMAGE" \
        -s dpiHeight 72.0 -s dpiWidth 72.0 \
        --out "$APP_ICONSET/icon_${size}x${size}.png" >/dev/null
    retina_size=$((size * 2))
    sips -s format png -z "$retina_size" "$retina_size" "$SOURCE_IMAGE" \
        -s dpiHeight 144.0 -s dpiWidth 144.0 \
        --out "$APP_ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done

# Let macOS encode the small bitmap/mask representations and Retina entries.
iconutil -c icns "$APP_ICONSET" -o "$WORK_DIR/AppIcon.icns"
mv "$WORK_DIR/AppIcon.icns" "$OUTPUT_ICON"
