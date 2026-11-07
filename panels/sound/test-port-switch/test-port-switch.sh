#!/bin/bash
# Usage: test-port-switch.sh SOUND_DIR CONFIG_H_DIR OUT_DIR
# Build test-port-switch.c against the gvc sources in SOUND_DIR and run it.
set -eu
SRC=$1
CFG=$2
OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
EXTRA=${EXTRA_CFLAGS:-}
PKGS="glib-2.0 gobject-2.0 gio-2.0 libpulse libpulse-mainloop-glib"
GVC="gvc-mixer-card.c gvc-mixer-stream.c gvc-channel-map.c gvc-mixer-ui-device.c
     gvc-mixer-sink.c gvc-mixer-source.c gvc-mixer-sink-input.c
     gvc-mixer-source-output.c gvc-mixer-event-role.c"

mkdir -p "$OUT"
for f in $GVC; do
    gcc -c -g -O1 -w $EXTRA -DG_LOG_DOMAIN=\"sound-cc-panel\" -I"$CFG" -I"$SRC" \
        $(pkg-config --cflags $PKGS) "$SRC/$f" -o "$OUT/${f%.c}.o"
done
gcc -g -O1 -w $EXTRA -DG_LOG_DOMAIN=\"sound-cc-panel\" -I"$CFG" -I"$SRC" \
    $(pkg-config --cflags $PKGS) "$HERE/test-port-switch.c" "$OUT"/*.o \
    $(pkg-config --libs $PKGS) -lm -o "$OUT/test-port-switch"
env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 "$OUT/test-port-switch" --verbose -k
