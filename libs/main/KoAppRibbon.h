/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KO_APP_RIBBON_H
#define KO_APP_RIBBON_H

#include "komain_export.h"

class KoModeBoxFactory;
class KoView;
class KoCanvasBase;

/** Configure the shared top ribbon with the application's XMLGUI commands.
 * Tool panels remain live, and menu actions retain their state and shortcuts.
 */
namespace KoAppRibbon
{
KOMAIN_EXPORT void configure(KoModeBoxFactory &factory, KoView *view, KoCanvasBase *canvas, bool spreadsheet);
}

#endif
