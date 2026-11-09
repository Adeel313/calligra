/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra developers
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "QRCodeDialog.h"

#include <KLocalizedString>

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{

QString colorStyleSheet(const QColor &color)
{
    return QStringLiteral("background-color: %1; border: 1px solid palette(mid); min-width: 3em;").arg(color.name());
}

} // namespace

QRCodeDialog::QRCodeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Insert QR Code"));
    setModal(true);

    m_content = new QPlainTextEdit;
    m_content->setPlaceholderText(i18n("Text or web address to encode"));
    m_content->setTabChangesFocus(true);
    m_content->setMinimumHeight(80);

    m_level = new QComboBox;
    m_level->addItem(i18n("Low (smallest code, ~7% recovery)"), int(QRCodeGenerator::Low));
    m_level->addItem(i18n("Medium (~15% recovery)"), int(QRCodeGenerator::Medium));
    m_level->addItem(i18n("Quartile (~25% recovery)"), int(QRCodeGenerator::Quartile));
    m_level->addItem(i18n("High (most robust, ~30% recovery)"), int(QRCodeGenerator::High));
    m_level->setCurrentIndex(1);

    m_width = new QDoubleSpinBox;
    m_width->setRange(0.5, 20.0);
    m_width->setSingleStep(0.5);
    m_width->setDecimals(1);
    m_width->setSuffix(i18n(" cm"));
    m_width->setValue(3.0);

    m_margin = new QSpinBox;
    m_margin->setRange(0, 8);
    m_margin->setValue(4);
    m_margin->setToolTip(i18n("Quiet zone around the code, in modules. Four is the value the standard asks for."));

    m_dark = new QPushButton;
    m_dark->setToolTip(i18n("Colour of the code"));
    m_light = new QPushButton;
    m_light->setToolTip(i18n("Colour behind the code"));
    m_dark->setStyleSheet(colorStyleSheet(m_darkColor));
    m_light->setStyleSheet(colorStyleSheet(m_lightColor));

    auto *colors = new QWidget;
    auto *colorsLayout = new QHBoxLayout(colors);
    colorsLayout->setContentsMargins(0, 0, 0, 0);
    colorsLayout->addWidget(m_dark);
    colorsLayout->addWidget(new QLabel(QStringLiteral("on")));
    colorsLayout->addWidget(m_light);
    colorsLayout->addStretch();

    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(180, 180);
    m_preview->setFrameShape(QFrame::StyledPanel);

    m_status = new QLabel;
    m_status->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(i18n("Content:"), m_content);
    form->addRow(i18n("Error correction:"), m_level);
    form->addRow(i18n("Width:"), m_width);
    form->addRow(i18n("Quiet zone:"), m_margin);
    form->addRow(i18n("Colours:"), colors);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form, 1);
    layout->addWidget(m_preview, 0, Qt::AlignHCenter);
    layout->addWidget(m_status);
    layout->addWidget(m_buttons);

    connect(m_content, &QPlainTextEdit::textChanged, this, &QRCodeDialog::updatePreview);
    connect(m_level, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_levelValue = QRCodeGenerator::ErrorCorrection(m_level->itemData(index).toInt());
        updatePreview();
    });
    connect(m_width, &QDoubleSpinBox::valueChanged, this, &QRCodeDialog::updatePreview);
    connect(m_margin, &QSpinBox::valueChanged, this, &QRCodeDialog::updatePreview);
    connect(m_dark, &QPushButton::clicked, this, &QRCodeDialog::chooseDarkColor);
    connect(m_light, &QPushButton::clicked, this, &QRCodeDialog::chooseLightColor);

    updatePreview();
    m_content->setFocus();
}

QRCodeDialog::~QRCodeDialog() = default;

void QRCodeDialog::chooseDarkColor()
{
    const QColor color = QColorDialog::getColor(m_darkColor, this, i18n("Colour of the Code"));
    if (color.isValid()) {
        m_darkColor = color;
        m_dark->setStyleSheet(colorStyleSheet(m_darkColor));
        updatePreview();
    }
}

void QRCodeDialog::chooseLightColor()
{
    const QColor color = QColorDialog::getColor(m_lightColor, this, i18n("Colour Behind the Code"));
    if (color.isValid()) {
        m_lightColor = color;
        m_light->setStyleSheet(colorStyleSheet(m_lightColor));
        updatePreview();
    }
}

void QRCodeDialog::updatePreview()
{
    const QString text = m_content->toPlainText();
    const QByteArray bytes = text.toUtf8();

    if (text.isEmpty()) {
        m_preview->setPixmap(QPixmap());
        m_status->setText(i18n("Enter the text or web address to encode."));
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }

    const int version = QRCodeGenerator::versionFor(bytes.size(), m_levelValue);
    if (version == 0) {
        m_preview->setPixmap(QPixmap());
        const int largest = QRCodeGenerator::capacity(QRCodeGenerator::maxVersion(), m_levelValue);
        m_status->setText(i18n("This text is too long: %1 of %2 bytes fit at this error correction level.",
                               int(bytes.size()), largest));
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }

    QString error;
    const QImage image = QRCodeGenerator::render(text, m_levelValue, 400, m_margin->value(), m_darkColor, m_lightColor, &error);
    if (image.isNull()) {
        m_preview->setPixmap(QPixmap());
        m_status->setText(error);
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }

    m_preview->setPixmap(QPixmap::fromImage(image).scaled(176, 176, Qt::KeepAspectRatio, Qt::FastTransformation));
    m_status->setText(i18n("Version %1, %2 of %3 bytes used.", version, int(bytes.size()),
                           QRCodeGenerator::capacity(version, m_levelValue)));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
}

QImage QRCodeDialog::image() const
{
    const QString text = m_content->toPlainText();
    if (text.isEmpty()) {
        return QImage();
    }

    const QVector<QVector<bool>> grid = QRCodeGenerator::modules(text, m_levelValue);
    if (grid.isEmpty()) {
        return QImage();
    }

    // Enough pixels per module that the code stays crisp when printed, and the
    // resolution is then set so the picture gets the width the user asked for.
    const int modulesAcross = grid.size() + 2 * m_margin->value();
    const int scale = qMax(8, 900 / modulesAcross);
    QImage image = QRCodeGenerator::render(text, m_levelValue, modulesAcross * scale, m_margin->value(),
                                           m_darkColor, m_lightColor);
    if (image.isNull()) {
        return image;
    }

    const qreal points = m_width->value() * 72.0 / 2.54;
    const int dotsPerMeter = qRound(image.width() * 2834.65 / qMax<qreal>(1.0, points));
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    return image;
}
