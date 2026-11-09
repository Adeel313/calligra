/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2012 C. Boemann <cbo@boemann.dk>
 * SPDX-FileCopyrightText: 2008 Jan Hambrecht <jaham@gmx.net>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KoShadowConfigWidget.h"
#include "ui_KoShadowConfigWidget.h"

#include <KoCanvasBase.h>
#include <KoCanvasResourceManager.h>
#include <KoColorPopupAction.h>
#include <KoIcon.h>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include <KoShapeShadow.h>
#include <KoShapeShadowCommand.h>
#include <KoUnit.h>

#include <KLocalizedString>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QWidgetAction>

#include <math.h>

namespace
{
// The Stage ribbon trades the full shadow panel for icons: a shape with a
// shadow for the toggle, and a menu for the geometry fields.
QPixmap shadowTogglePixmap()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawRect(QRectF(5, 5, 9, 9));
    painter.setPen(QPen(QColor(QStringLiteral("#555555")), 1.0));
    painter.setBrush(QColor(QStringLiteral("#f2f2f2")));
    painter.drawRect(QRectF(2, 2, 9, 9));
    return pixmap;
}

/// Take a control out of whichever layout of the panel currently holds it.
void detachFromLayout(QWidget *widget)
{
    if (!widget || !widget->parentWidget()) {
        return;
    }
    const QList<QLayout *> layouts = widget->parentWidget()->findChildren<QLayout *>();
    for (QLayout *layout : layouts) {
        layout->removeWidget(widget);
    }
}
}

class Q_DECL_HIDDEN KoShadowConfigWidget::Private
{
public:
    Private() = default;
    Ui_KoShadowConfigWidget widget;
    KoColorPopupAction *actionShadowColor;
    KoCanvasBase *canvas;
};

KoShadowConfigWidget::KoShadowConfigWidget(QWidget *parent)
    : QWidget(parent)
    , d(new Private())
{
    d->widget.setupUi(this);
    d->widget.shadowOffset->setValue(8.0);
    d->widget.shadowBlur->setValue(8.0);
    d->widget.shadowBlur->setMinimum(0.0);
    d->widget.shadowAngle->setValue(315.0);
    d->widget.shadowAngle->setMinimum(0.0);
    d->widget.shadowAngle->setMaximum(360.0);
    d->widget.shadowVisible->setChecked(false);
    visibilityChanged();

    // The Sheets ribbon keeps its controls in two rows: a smaller angle dial
    // and no expanding spacer keeps the Shadow group as flat as its neighbours.
    if (QCoreApplication::applicationName().contains(QLatin1String("sheets"), Qt::CaseInsensitive)) {
        d->widget.shadowAngle->setMaximumSize(28, 28);
        d->widget.SpecialSpacer->hide();
    }

    // The Stage Format tab keeps a row of icons: the on/off toggle, the colour
    // swatch, and a menu holding the blur, distance and angle controls.
    if (QCoreApplication::applicationName().contains(QLatin1String("stage"), Qt::CaseInsensitive)) {
        d->widget.SpecialSpacer->hide();
        d->widget.label_2->hide();
        d->widget.label->hide();

        auto *toggle = new QToolButton(this);
        toggle->setCheckable(true);
        toggle->setAutoRaise(true);
        toggle->setIcon(QIcon(shadowTogglePixmap()));
        toggle->setIconSize(QSize(16, 16));
        toggle->setToolTip(i18n("Use a shadow"));
        toggle->setChecked(d->widget.shadowVisible->isChecked());
        d->widget.shadowVisible->hide();
        connect(toggle, &QToolButton::toggled, d->widget.shadowVisible, &QCheckBox::setChecked);
        connect(d->widget.shadowVisible, &QCheckBox::toggled, toggle, &QToolButton::setChecked);
        d->widget.gridLayout_3->addWidget(toggle, 0, 0);

        auto *options = new QToolButton(this);
        options->setAutoRaise(true);
        options->setPopupMode(QToolButton::InstantPopup);
        options->setIcon(koIcon("configure"));
        options->setIconSize(QSize(16, 16));
        options->setToolTip(i18n("Shadow distance, blur and angle"));
        auto *menu = new QMenu(options);
        options->setMenu(menu);

        auto addField = [menu](const QString &label, QWidget *field) {
            detachFromLayout(field);
            auto *row = new QWidget(menu);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(6, 2, 6, 2);
            rowLayout->setSpacing(6);
            auto *caption = new QLabel(label, row);
            rowLayout->addWidget(caption);
            field->setParent(row);
            rowLayout->addWidget(field, 1);
            auto *action = new QWidgetAction(menu);
            action->setDefaultWidget(row);
            menu->addAction(action);
        };
        addField(i18n("Distance"), d->widget.shadowOffset);
        addField(i18n("Blur"), d->widget.shadowBlur);
        addField(i18n("Angle"), d->widget.shadowAngle);
        d->widget.gridLayout_3->addWidget(options, 0, 3, Qt::AlignVCenter);
    }

    d->actionShadowColor = new KoColorPopupAction(this);
    d->actionShadowColor->setCurrentColor(QColor(0, 0, 0, 192)); // some reasonable default for shadow
    d->actionShadowColor->setIcon(koIcon("format-stroke-color"));
    d->actionShadowColor->setToolTip(i18n("Change the color of the shadow"));
    d->widget.shadowColor->setDefaultAction(d->actionShadowColor);

    connect(d->widget.shadowVisible, &QAbstractButton::toggled, this, &KoShadowConfigWidget::applyChanges);
    connect(d->widget.shadowVisible, &QAbstractButton::toggled, this, &KoShadowConfigWidget::visibilityChanged);
    connect(d->actionShadowColor, &KoColorPopupAction::colorChanged, this, &KoShadowConfigWidget::applyChanges);
    connect(d->widget.shadowAngle, &QAbstractSlider::valueChanged, this, &KoShadowConfigWidget::applyChanges);
    connect(d->widget.shadowOffset, &KoUnitDoubleSpinBox::valueChangedPt, this, &KoShadowConfigWidget::applyChanges);
    connect(d->widget.shadowBlur, &KoUnitDoubleSpinBox::valueChangedPt, this, &KoShadowConfigWidget::applyChanges);
}

KoShadowConfigWidget::~KoShadowConfigWidget()
{
    delete d;
}

void KoShadowConfigWidget::setShadowColor(const QColor &color)
{
    d->widget.shadowColor->blockSignals(true);
    d->actionShadowColor->blockSignals(true);

    d->actionShadowColor->setCurrentColor(color);

    d->actionShadowColor->blockSignals(false);
    d->widget.shadowColor->blockSignals(false);
}

QColor KoShadowConfigWidget::shadowColor() const
{
    return d->actionShadowColor->currentColor();
}

void KoShadowConfigWidget::setShadowOffset(const QPointF &offset)
{
    qreal length = sqrt(offset.x() * offset.x() + offset.y() * offset.y());
    qreal angle = atan2(-offset.y(), offset.x());
    if (angle < 0.0) {
        angle += 2 * M_PI;
    }

    d->widget.shadowAngle->blockSignals(true);
    d->widget.shadowAngle->setValue(-90 - angle * 180.0 / M_PI);
    d->widget.shadowAngle->blockSignals(false);

    d->widget.shadowOffset->blockSignals(true);
    d->widget.shadowOffset->changeValue(length);
    d->widget.shadowOffset->blockSignals(false);
}

QPointF KoShadowConfigWidget::shadowOffset() const
{
    QPointF offset(d->widget.shadowOffset->value(), 0);
    QTransform m;
    m.rotate(d->widget.shadowAngle->value() + 90);
    return m.map(offset);
}

void KoShadowConfigWidget::setShadowBlur(const qreal &blur)
{
    d->widget.shadowBlur->blockSignals(true);
    d->widget.shadowBlur->changeValue(blur);
    d->widget.shadowBlur->blockSignals(false);
}

qreal KoShadowConfigWidget::shadowBlur() const
{
    return d->widget.shadowBlur->value();
}

void KoShadowConfigWidget::setShadowVisible(bool visible)
{
    d->widget.shadowVisible->blockSignals(true);
    d->widget.shadowVisible->setChecked(visible);
    d->widget.shadowVisible->blockSignals(false);
    visibilityChanged();
}

bool KoShadowConfigWidget::shadowVisible() const
{
    return d->widget.shadowVisible->isChecked();
}

void KoShadowConfigWidget::visibilityChanged()
{
    d->widget.shadowAngle->setEnabled(d->widget.shadowVisible->isChecked());
    d->widget.shadowBlur->setEnabled(d->widget.shadowVisible->isChecked());
    d->widget.shadowColor->setEnabled(d->widget.shadowVisible->isChecked());
    d->widget.shadowOffset->setEnabled(d->widget.shadowVisible->isChecked());
}

void KoShadowConfigWidget::applyChanges()
{
    if (d->canvas) {
        KoSelection *selection = d->canvas->shapeManager()->selection();
        KoShape *shape = selection->firstSelectedShape(KoFlake::TopLevelSelection);
        if (!shape) {
            return;
        }

        KoShapeShadow *newShadow = new KoShapeShadow();
        newShadow->setVisible(shadowVisible());
        newShadow->setColor(shadowColor());
        newShadow->setOffset(shadowOffset());
        newShadow->setBlur(shadowBlur());
        d->canvas->addCommand(new KoShapeShadowCommand(selection->selectedShapes(KoFlake::TopLevelSelection), newShadow));
    }
}

void KoShadowConfigWidget::selectionChanged()
{
    if (!d->canvas) {
        return;
    }

    KoSelection *selection = d->canvas->shapeManager()->selection();
    KoShape *shape = selection->firstSelectedShape(KoFlake::TopLevelSelection);

    setEnabled(shape != nullptr);

    if (!shape) {
        setShadowVisible(false);
        return;
    }

    KoShapeShadow *shadow = shape->shadow();
    if (!shadow) {
        setShadowVisible(false);
        return;
    }

    setShadowVisible(shadow->isVisible());
    setShadowOffset(shadow->offset());
    setShadowColor(shadow->color());
    setShadowBlur(shadow->blur());
}

void KoShadowConfigWidget::setCanvas(KoCanvasBase *canvas)
{
    d->canvas = canvas;
    connect(canvas->shapeManager(), &KoShapeManager::selectionChanged, this, &KoShadowConfigWidget::selectionChanged);
    connect(canvas->shapeManager(), &KoShapeManager::selectionContentChanged, this, &KoShadowConfigWidget::selectionChanged);

    setUnit(canvas->unit());

    connect(d->canvas->resourceManager(), &KoCanvasResourceManager::canvasResourceChanged, this, &KoShadowConfigWidget::resourceChanged);
}

void KoShadowConfigWidget::setUnit(const KoUnit &unit)
{
    d->widget.shadowOffset->blockSignals(true);
    d->widget.shadowBlur->blockSignals(true);
    d->widget.shadowOffset->setUnit(unit);
    d->widget.shadowBlur->setUnit(unit);
    d->widget.shadowOffset->blockSignals(false);
    d->widget.shadowBlur->blockSignals(false);
}

void KoShadowConfigWidget::resourceChanged(int key, const QVariant &res)
{
    if (key == KoCanvasResourceManager::Unit) {
        setUnit(res.value<KoUnit>());
    }
}
