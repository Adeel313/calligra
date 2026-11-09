/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "KWPrintDialog.h"

#include <KoPrintJob.h>

#include <KLocalizedString>

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPageSize>
#include <QPrintPreviewWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

class KWPrintPageLayoutWidget : public QWidget
{
public:
    explicit KWPrintPageLayoutWidget(KoPrintJob *printJob, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_printJob(printJob)
    {
        setWindowTitle(i18n("Page Layout"));

        auto *form = new QFormLayout(this);
        m_orientation = new QComboBox(this);
        m_orientation->addItem(i18n("Portrait"), int(QPageLayout::Portrait));
        m_orientation->addItem(i18n("Landscape"), int(QPageLayout::Landscape));
        form->addRow(i18n("Orientation:"), m_orientation);

        m_paperSize = new QComboBox(this);
        addPaperSize(QPageSize::A4, i18n("A4 (210 x 297 mm)"));
        addPaperSize(QPageSize::Letter, i18n("Letter (8.5 x 11 in)"));
        addPaperSize(QPageSize::Legal, i18n("Legal (8.5 x 14 in)"));
        addPaperSize(QPageSize::A5, i18n("A5 (148 x 210 mm)"));
        addPaperSize(QPageSize::A3, i18n("A3 (297 x 420 mm)"));
        addPaperSize(QPageSize::Executive, i18n("Executive (7.25 x 10.5 in)"));
        form->addRow(i18n("Paper size:"), m_paperSize);

        m_margins = new QComboBox(this);
        m_margins->addItem(i18n("Document margins"), 0);
        m_margins->addItem(i18n("None"), 1);
        m_margins->addItem(i18n("Narrow (0.5 in)"), 2);
        m_margins->addItem(i18n("Normal (1 in)"), 3);
        m_margins->addItem(i18n("Wide (1.5 in)"), 4);
        form->addRow(i18n("Margins:"), m_margins);

        initializeFromPrinter();
        connect(m_orientation, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyToPrinter(); });
        connect(m_paperSize, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyToPrinter(); });
        connect(m_margins, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyToPrinter(); });
    }

private:
    void addPaperSize(QPageSize::PageSizeId id, const QString &label)
    {
        m_paperSize->addItem(label, int(id));
    }

    void initializeFromPrinter()
    {
        const QPageLayout layout = m_printJob->printer().pageLayout();
        m_initializing = true;
        int orientationIndex = m_orientation->findData(int(layout.orientation()));
        if (orientationIndex >= 0)
            m_orientation->setCurrentIndex(orientationIndex);
        int paperIndex = m_paperSize->findData(int(layout.pageSize().id()));
        if (paperIndex >= 0)
            m_paperSize->setCurrentIndex(paperIndex);
        m_initializing = false;
    }

    void applyToPrinter()
    {
        if (m_initializing)
            return;

        QPrinter &printer = m_printJob->printer();
        const auto paperSize = static_cast<QPageSize::PageSizeId>(m_paperSize->currentData().toInt());
        const auto orientation = static_cast<QPageLayout::Orientation>(m_orientation->currentData().toInt());
        printer.setPageSize(QPageSize(paperSize));
        printer.setPageOrientation(orientation);

        qreal marginInch = 0.0;
        switch (m_margins->currentData().toInt()) {
        case 2:
            marginInch = 0.5;
            break;
        case 3:
            marginInch = 1.0;
            break;
        case 4:
            marginInch = 1.5;
            break;
        default:
            break;
        }
        printer.setPageMargins(QMarginsF(marginInch, marginInch, marginInch, marginInch), QPageLayout::Inch);
        m_printJob->setProperty("printerPageLayout", true);
    }

    KoPrintJob *m_printJob;
    QComboBox *m_orientation = nullptr;
    QComboBox *m_paperSize = nullptr;
    QComboBox *m_margins = nullptr;
    bool m_initializing = false;
};

KWPrintDialog::KWPrintDialog(KoPrintJob *printJob, QWidget *parent)
    : QPrintDialog(&printJob->printer(), parent)
    , m_printJob(printJob)
{
    QList<QWidget *> optionWidgets = printJob->createOptionWidgets();
    optionWidgets.append(new KWPrintPageLayoutWidget(printJob));
    setOptionTabs(optionWidgets);
    setMinMax(printJob->printer().fromPage(), printJob->printer().toPage());
    setOptions(printJob->printDialogOptions());
}

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
class KWPrintDialogWindow : public QDialog
{
public:
    KWPrintDialogWindow(QPrintDialog *options, KoPrintJob *printJob, QWidget *parent)
        : QDialog(parent)
        , m_options(options)
        , m_printJob(printJob)
    {
        setWindowTitle(i18n("Print"));
        setModal(true);

        auto *mainLayout = new QVBoxLayout(this);
        auto *contentLayout = new QHBoxLayout;

        auto *previewLayout = new QVBoxLayout;
        m_preview = new QPrintPreviewWidget(&m_printJob->printer(), this);
        m_preview->setViewMode(QPrintPreviewWidget::SinglePageView);
        m_preview->setZoomMode(QPrintPreviewWidget::FitInView);
        m_preview->setMinimumSize(300, 400);
        previewLayout->addWidget(m_preview, 1);

        auto *navigation = new QHBoxLayout;
        auto *previous = new QToolButton(this);
        previous->setAutoRaise(true);
        previous->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
        auto *next = new QToolButton(this);
        next->setAutoRaise(true);
        next->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
        m_pageLabel = new QLabel(this);
        m_pageLabel->setAlignment(Qt::AlignCenter);
        navigation->addWidget(previous);
        navigation->addWidget(m_pageLabel, 1);
        navigation->addWidget(next);
        previewLayout->addLayout(navigation);

        contentLayout->addLayout(previewLayout, 2);

        m_options->setParent(this);
        m_options->setWindowFlags(Qt::Widget);
        if (auto *buttonBox = m_options->findChild<QDialogButtonBox *>()) {
            for (QAbstractButton *button : buttonBox->buttons()) {
                const auto role = buttonBox->buttonRole(button);
                if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::RejectRole)
                    button->hide();
            }
        }
        m_optionsScroll = new QScrollArea(this);
        m_optionsScroll->setFrameShape(QFrame::NoFrame);
        m_optionsScroll->setWidgetResizable(true);
        m_optionsScroll->setWidget(m_options);
        m_optionsScroll->setMinimumWidth(440);
        contentLayout->addWidget(m_optionsScroll, 3);
        mainLayout->addLayout(contentLayout, 1);

        auto *previewUpdate = new QTimer(this);
        previewUpdate->setSingleShot(true);
        previewUpdate->setInterval(250);
        connect(previewUpdate, &QTimer::timeout, m_preview, &QPrintPreviewWidget::updatePreview);
        const auto schedulePreviewUpdate = [previewUpdate]() { previewUpdate->start(); };
        for (QComboBox *combo : m_options->findChildren<QComboBox *>())
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, schedulePreviewUpdate);
        for (QSpinBox *spinBox : m_options->findChildren<QSpinBox *>())
            connect(spinBox, qOverload<int>(&QSpinBox::valueChanged), this, schedulePreviewUpdate);
        for (QAbstractButton *button : m_options->findChildren<QAbstractButton *>())
            connect(button, &QAbstractButton::toggled, this, schedulePreviewUpdate);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        auto *printButton = buttons->addButton(i18n("Print"), QDialogButtonBox::AcceptRole);
        printButton->setDefault(true);
        mainLayout->addWidget(buttons);

        connect(previous, &QToolButton::clicked, this, [this]() {
            m_preview->setCurrentPage(qMax(1, m_preview->currentPage() - 1));
        });
        connect(next, &QToolButton::clicked, this, [this]() {
            m_preview->setCurrentPage(qMin(m_preview->pageCount(), m_preview->currentPage() + 1));
        });
        connect(m_preview, &QPrintPreviewWidget::previewChanged, this, &KWPrintDialogWindow::updatePageLabel);
        connect(buttons, &QDialogButtonBox::accepted, this, &KWPrintDialogWindow::print);
        connect(buttons, &QDialogButtonBox::rejected, this, &KWPrintDialogWindow::reject);

        m_printJob->setProperty("blocking", true);
        connect(m_preview, &QPrintPreviewWidget::paintRequested, m_printJob, [this](QPrinter *) {
            m_printJob->startPrinting();
        });

        resize(860, 540);
        updatePageLabel();

        QTimer::singleShot(0, this, [this]() {
            QRect frame = frameGeometry();
            frame.moveCenter(screen()->availableGeometry().center());
            move(frame.topLeft());
        });

        // Open the standard Copies/Options pane so the print settings are
        // visible immediately, as they are in LibreOffice's print dialog.
        QTimer::singleShot(0, this, [this]() {
            if (!QMetaObject::invokeMethod(m_options, "_q_collapseOrExpandDialog")) {
                const auto buttons = m_options->findChildren<QAbstractButton *>();
                for (QAbstractButton *button : buttons) {
                    if (button->text().contains(QStringLiteral("Options"))) {
                        button->click();
                        break;
                    }
                }
            }
        });
    }

    ~KWPrintDialogWindow() override
    {
        m_printJob->setProperty("blocking", false);
    }

    /// The options page belongs to the KWPrintDialog the caller owns, so it
    /// must not be deleted with this window.
    void releaseOptionsPage()
    {
        if (m_optionsScroll) {
            m_optionsScroll->takeWidget();
        }
        m_options->setParent(nullptr);
        m_options->hide();
    }

    void reject() override
    {
        m_printJob->setProperty("blocking", false);
        QDialog::reject();
    }

private:
    void updatePageLabel()
    {
        const int pageCount = m_preview->pageCount();
        const int currentPage = qBound(1, m_preview->currentPage(), qMax(1, pageCount));
        m_pageLabel->setText(i18n("Page %1 of %2", currentPage, pageCount));
    }

    void print()
    {
        m_options->accept();
        if (m_options->result() != QDialog::Accepted)
            return;

        m_printJob->setProperty("blocking", false);
        QDialog::accept();
    }

    QPrintDialog *m_options;
    QScrollArea *m_optionsScroll = nullptr;
    KoPrintJob *m_printJob;
    QPrintPreviewWidget *m_preview = nullptr;
    QLabel *m_pageLabel = nullptr;
};
#endif

int KWPrintDialog::exec()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    KWPrintDialogWindow window(this, m_printJob, parentWidget());
    const int result = window.exec();
    // The window owns nothing that outlives it, least of all this dialog.
    window.releaseOptionsPage();
    return result;
#else
    return QPrintDialog::exec();
#endif
}