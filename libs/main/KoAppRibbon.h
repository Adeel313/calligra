/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KO_APP_RIBBON_H
#define KO_APP_RIBBON_H

#include "komain_export.h"

#include <QIcon>
#include <QString>

class KoModeBoxFactory;
class KoView;
class KoCanvasBase;

/** Configure the shared top ribbon with the application's XMLGUI commands.
 * Tool panels remain live, and menu actions retain their state and shortcuts.
 */
namespace KoAppRibbon
{
KOMAIN_EXPORT void configure(KoModeBoxFactory &factory, KoView *view, KoCanvasBase *canvas, bool spreadsheet);

/** The bundled, colour-tinted ribbon icon of an action, or a null icon when the
 * action has no symbol.  Callers fall back to the desktop icon theme.
 */
KOMAIN_EXPORT QIcon commandIcon(const QString &actionName);

/** Give the view's actions (including the lazily created tool actions) the
 * bundled ribbon icons.  Actions keep their handlers and state.
 */
KOMAIN_EXPORT void applyCommandIcons(KoView *view);
}

#endif
