/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2011 Jan Hambrecht <jaham@gmx.net>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ArtisticTextShapeOnPathWidget.h"
#include "ArtisticTextTool.h"
#include "ui_ArtisticTextShapeOnPathWidget.h"
#include <QAction>
#include <QApplication>
#include <KoRibbonLayout.h>

ArtisticTextShapeOnPathWidget::ArtisticTextShapeOnPathWidget(ArtisticTextTool *tool, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ArtisticTextShapeOnPathWidget)
    , m_textTool(tool)
{
    ui->setupUi(this);
    ui->detachFromPath->setDefaultAction(tool->action("artistictext_detach_from_path"));
    ui->convertToPath->setDefaultAction(tool->action("artistictext_convert_to_path"));

    if (qApp->applicationName().contains(QLatin1String("words"), Qt::CaseInsensitive)) {
        ui->line->hide();
        ui->gridLayout->removeWidget(ui->detachFromPath);
        ui->gridLayout->removeWidget(ui->convertToPath);
        ui->gridLayout->removeWidget(ui->startOffset);
        KoRibbonLayout::configureButton(ui->detachFromPath, Qt::ToolButtonTextBesideIcon);
        KoRibbonLayout::configureButton(ui->convertToPath, Qt::ToolButtonTextBesideIcon);
        ui->gridLayout->addWidget(ui->detachFromPath, 0, 0);
        ui->gridLayout->addWidget(ui->convertToPath, 1, 0);
        ui->startOffset->setAccessibleName(tr("Text position on path"));
        ui->startOffset->setMaximumWidth(150);
        ui->gridLayout->addWidget(ui->startOffset, 0, 1, 2, 1);
    }

    connect(ui->startOffset, &QAbstractSlider::valueChanged, this, &ArtisticTextShapeOnPathWidget::offsetChanged);
}

ArtisticTextShapeOnPathWidget::~ArtisticTextShapeOnPathWidget()
{
    delete ui;
}

void ArtisticTextShapeOnPathWidget::updateWidget()
{
    ArtisticTextToolSelection *selection = dynamic_cast<ArtisticTextToolSelection *>(m_textTool->selection());
    if (!selection)
        return;

    ArtisticTextShape *currentText = selection->selectedShape();
    if (!currentText)
        return;

    ui->startOffset->blockSignals(true);
    ui->startOffset->setValue(static_cast<int>(currentText->startOffset() * 100.0));
    ui->startOffset->setEnabled(currentText->isOnPath());
    ui->startOffset->blockSignals(false);
}
