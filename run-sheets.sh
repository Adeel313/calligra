#!/bin/sh
# Launch Calligra-NG Sheets from this build-opt/bin directory.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$ROOT/build-opt/bin"

DATA_ROOT=${CALLIGRA_DATA_ROOT:-}
if [ -z "$DATA_ROOT" ]; then
    for p in \
        /opt/calligra-ng \
        /tmp/opencode/calligra-ng-deb/root/opt/calligra-ng \
        "$ROOT/build-opt/local-install" \
        "$ROOT/build/install" \
        "$ROOT/build-opt/install" \
        /usr/local \
        /usr
    do
        if [ -d "$p/share/calligra" ] || [ -d "$p/share/calligrasheets" ] || [ -f "$p/share/kxmlgui5/calligrasheets/calligrasheets.rc" ] || [ -d "$p/lib/x86_64-linux-gnu/calligra" ] || [ -d "$p/lib/calligra" ]; then
            DATA_ROOT=$p
            break
        fi
    done
fi
if [ -z "$DATA_ROOT" ]; then
    # Last-resort fallback: run with system paths and warn.
    DATA_ROOT=/usr
    echo "Warning: Calligra-NG data files not found; using DATA_ROOT=$DATA_ROOT. Set CALLIGRA_DATA_ROOT if needed." >&2
fi

for d in "$DATA_ROOT/lib/x86_64-linux-gnu" "$DATA_ROOT/lib"; do
    if [ -d "$d" ]; then
        LIBDIR=$d
        break
    fi
done
LIBDIR=${LIBDIR:-"$DATA_ROOT/lib/x86_64-linux-gnu"}
APP="$BIN/calligrasheets"

if [ ! -x "$APP" ]; then
    echo "Missing executable: $APP" >&2
    exit 1
fi

exec env \
    LD_LIBRARY_PATH="$BIN:$LIBDIR:$DATA_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    QT_PLUGIN_PATH="$BIN:$LIBDIR/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" \
    XDG_DATA_DIRS="$DATA_ROOT/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" \
    XDG_CONFIG_DIRS="$DATA_ROOT/etc/xdg:${XDG_CONFIG_DIRS:-/etc/xdg}" \
    QML2_IMPORT_PATH="$LIBDIR/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}" \
    QT_QUICK_CONTROLS_STYLE_PATH="$LIBDIR/qml/QtQuick/Controls.2${QT_QUICK_CONTROLS_STYLE_PATH:+:$QT_QUICK_CONTROLS_STYLE_PATH}" \
    "$APP" "$@"
