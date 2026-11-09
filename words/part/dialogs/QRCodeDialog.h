/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra developers
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef QRCODEDIALOG_H
#define QRCODEDIALOG_H

#include "QRCodeGenerator.h"

#include <QDialog>
#include <QImage>

class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

/**
 * Asks for the content and the looks of a QR code and renders it.
 */
class QRCodeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QRCodeDialog(QWidget *parent = nullptr);
    ~QRCodeDialog() override;

    /// The code at print resolution, sized to the chosen width in centimetres.
    QImage image() const;

private:
    void updatePreview();
    void chooseDarkColor();
    void chooseLightColor();

    QPlainTextEdit *m_content;
    QComboBox *m_level;
    QDoubleSpinBox *m_width;
    QSpinBox *m_margin;
    QPushButton *m_dark;
    QPushButton *m_light;
    QLabel *m_preview;
    QLabel *m_status;
    QDialogButtonBox *m_buttons;

    QRCodeGenerator::ErrorCorrection m_levelValue = QRCodeGenerator::Medium;
    QColor m_darkColor = Qt::black;
    QColor m_lightColor = Qt::white;
};

#endif
