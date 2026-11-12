/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2008 Stefan Nikolaus <stefan.nikolaus@kdemail.net>
 * SPDX-FileCopyrightText: 2007 Thomas Zander <zander@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef PRINTJOB_H
#define PRINTJOB_H

#include <KoPrintingDialog.h>

struct KoPageLayout;
class QPrinter;

namespace Calligra
{
namespace Sheets
{
class View;

/**
 * Manages printing on the document level.
 */
class PrintJob : public KoPrintingDialog
{
public:
    /**
     * @param view the view being printed
     * @param printAllSheets print every sheet instead of only the active one,
     *        used by non-interactive exports such as PDF
     */
    explicit PrintJob(View *view, bool printAllSheets = false);
    ~PrintJob() override;

    int documentFirstPage() const override;
    int documentLastPage() const override;
    QAbstractPrintDialog::PrintDialogOptions printDialogOptions() const override;

public Q_SLOTS:
    void startPrinting(RemovePolicy removePolicy = DoNotDelete) override;

protected:
    QRectF preparePage(int pageNumber) override;
    void printPage(int pageNumber, QPainter &painter) override;
    QList<KoShape *> shapesOnPage(int pageNumber) override;
    QList<QWidget *> createOptionWidgets() const override;

private:
    //! Applies a sheet page layout (paper size, orientation, margins) to the printer.
    static void applyPageLayout(QPrinter &printer, const KoPageLayout &pageLayout);

    class Private;
    Private *const d;
};

} // namespace Sheets
} // namespace Calligra

#endif
