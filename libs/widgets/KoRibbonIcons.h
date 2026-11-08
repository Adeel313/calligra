/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KO_RIBBON_ICONS_H
#define KO_RIBBON_ICONS_H

#include "kowidgets_export.h"

#include <QIcon>
#include <QString>

/**
 * The colour-tinted Material Symbols shipped with the ribbon.
 *
 * Commands are looked up by their action object name, so the same command gets
 * the same icon in Sheets, Words and Stage. Tool panels that create plain
 * buttons (without a QAction) use forThemeIcon() with the desktop icon name
 * they already pass to the icon theme.
 */
namespace KoRibbonIcons
{
/// Icon of a ribbon command (action object name) or of a symbol file name.
KOWIDGETS_EXPORT QIcon forCommand(const QString &name);

/// Icon of a desktop icon-theme name, tinted like the ribbon command it
/// represents. Falls back to \a fallback, then to the icon theme itself.
KOWIDGETS_EXPORT QIcon forThemeIcon(const QString &themeName, const QIcon &fallback = QIcon());
}

#endif
