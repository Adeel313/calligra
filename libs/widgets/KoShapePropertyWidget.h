// SPDX-FileCopyrightText: 2024 Carl Schwan <carl@carlschwan.eu>
// SPDX-License-Identifier: LGPL-2.0-or-later

#pragma once

#include "kowidgets_export.h"

#include <QWidget>

class KoCanvasBase;
class KoShape;

class KOWIDGETS_EXPORT KoShapePropertyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KoShapePropertyWidget(QWidget *widget = nullptr);
    ~KoShapePropertyWidget();

    void setCanvas(KoCanvasBase *canvas);

    /// True while a shape's settings are shown instead of the placeholder.
    bool hasProperties() const;

Q_SIGNALS:
    /// Emitted when the widget gains or loses the shape settings it shows.
    void propertiesChanged(bool hasProperties);

private:
    void selectionChanged();
    void addWidgetForShape(KoShape *shape);
    void shapePropertyChanged();
    void canvasResourceChanged(int key, const QVariant &variant);
    void addPlaceholder();

    class Private;
    std::unique_ptr<Private> d;
};
