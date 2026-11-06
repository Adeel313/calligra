#!/bin/sh
# Launch the locally installed Calligra Stage, including its plugins and data.
set -e
SRC="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
D="$SRC/destdir/usr"
exec env \
    LD_LIBRARY_PATH="$D/lib/x86_64-linux-gnu:$D/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    QT_PLUGIN_PATH="$D/lib/x86_64-linux-gnu/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" \
    XDG_DATA_DIRS="$D/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" \
    "$D/bin/calligrastage" "$@"
