/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2010 Benjamin Port <port.benjamin@gmail.com>
   SPDX-FileCopyrightText: 2009 Thorsten Zachmann <zachmann@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "KPrPdfPrintJob.h"

#include "KPrView.h"
#include "KoPAPageBase.h"
#include "KoPAPageProvider.h"
#include "KoPAUtil.h"
#include <KoPageLayout.h>
#include <KoZoomHandler.h>
#include <QPainter>
KPrPdfPrintJob::KPrPdfPrintJob(KPrView *view)
    : KoPAPrintJob(view)
{
}

KPrPdfPrintJob::~KPrPdfPrintJob() = default;

void KPrPdfPrintJob::startPrinting(RemovePolicy removePolicy)
{
    int fromPage = m_printer.fromPage() > 0 ? m_printer.fromPage() - 1 : 0;
    int toPage = m_printer.toPage() > 0 ? m_printer.toPage() - 1 : m_pages.size() - 1;

    Q_ASSERT(fromPage >= 0 && fromPage < m_pages.size());
    Q_ASSERT(toPage >= 0 && toPage < m_pages.size());

    KoZoomHandler zoomHandler;
    zoomHandler.setResolution(m_printer.resolution(), m_printer.resolution());
    m_printer.setFullPage(true);
    const KoPageLayout &firstLayout = m_pages.at(fromPage)->pageLayout();
    // KoPageLayout sizes are in points; declaring them as millimetres scaled every exported page by 2.83.
    m_printer.setPageSize(QPageSize(QSizeF(firstLayout.width, firstLayout.height), QPageSize::Point));
    QPainter painter(&m_printer);

    for (int i = fromPage; i <= toPage; ++i) {
        KoPAPageBase *page = m_pages.at(i);
        const KoPageLayout &layout = page->pageLayout();
        m_printer.setPageSize(QPageSize(QSizeF(layout.width, layout.height), QPageSize::Point));
        // KoPAUtil::setZoom() compares the page size against layouts in points, so the printable
        // area has to be in device pixels. Asking for it in the default unit gave millimetres and
        // scaled every slide down to about a quarter of the page.
        const qreal dpi = m_printer.resolution();
        const QRectF paintRect = m_printer.pageLayout().paintRect(QPageLayout::Point);
        const QSizeF size(paintRect.width() * dpi / 72.0, paintRect.height() * dpi / 72.0);
        painter.save();
        if (i != fromPage) {
            m_printer.newPage();
        }
        KoPAUtil::setZoom(layout, size, zoomHandler);
        QRect pageRect(KoPAUtil::pageRect(layout, size, zoomHandler));
        painter.setClipRect(pageRect);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(pageRect.topLeft());
        m_pageProvider->setPageData(i + 1, page);
        page->paintPage(painter, zoomHandler);
        painter.restore();
    }

    if (removePolicy == DeleteWhenDone) {
        deleteLater();
    }
}
