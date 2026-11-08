#!/bin/sh
# Launch the locally built Calligra Words.
#
# The binary installed into ./destdir needs three paths that a plain
# `./build/bin/calligrawords` or `./destdir/usr/bin/calligrawords` run does not
# have: the private libs, the calligra plugin tree (QT_PLUGIN_PATH -- without
# it the "parts" plugin is not found and the app aborts with "Essential
# application components could not be found"), and the installed data files
# such as calligrawords.rc (XDG_DATA_DIRS).
#
# After rebuilding:  DESTDIR=$PWD/destdir ninja -C build install

set -e

SRC="$(cd "$(dirname "$0")" && pwd)"
D="$SRC/destdir/usr"
BUILD="$SRC/build/bin"

if [ ! -x "$D/bin/calligrawords" ]; then
    echo "No local install at $D/bin/calligrawords" >&2
    echo "Build and install first:" >&2
    echo "    DESTDIR=$SRC/destdir ninja -C $SRC/build install" >&2
    exit 1
fi

exec env \
    LD_LIBRARY_PATH="$BUILD:$D/lib/x86_64-linux-gnu:$D/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    QT_PLUGIN_PATH="$D/lib/x86_64-linux-gnu/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" \
    XDG_DATA_DIRS="$D/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" \
    "$BUILD/calligrawords" "$@"
