/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef KWPRINTDIALOG_H
#define KWPRINTDIALOG_H

#include <QPrintDialog>

class KoPrintJob;

/** Print dialog with an integrated preview of the document. */
class KWPrintDialog : public QPrintDialog
{
public:
    KWPrintDialog(KoPrintJob *printJob, QWidget *parent = nullptr);

    int exec() override;

private:
    KoPrintJob *m_printJob;
};

#endif