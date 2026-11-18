/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra contributors <info@calligra.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef TESTODFEXPORTFONTINHERITANCE_H
#define TESTODFEXPORTFONTINHERITANCE_H

#include <QObject>

class TestOdfExportFontInheritance : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testInheritedFontIsNotWrittenToAutoStyle();
    void testFontOverridingParentStyleIsWritten();
};

#endif
