/*
 *  SPDX-FileCopyrightText: 2026 Calligra developers
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "KWPageSetupGallery.h"

#include <KLocalizedString>

#include <QEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidgetAction>

#include <functional>

namespace
{
/// The canvas every page picture is painted on; the pixmap is rendered at twice
/// the size so that the pages stay crisp.
const QSize PreviewSize(84, 46);
/// The height of the strip with the arrow of a gallery entry.
const int ArrowStripHeight = 12;
/// The height of one line of a caption.
const int LineHeight = 13;
/// The width of every gallery entry, wide enough for its caption.
const int TileWidth = 104;

/// The margin presets, in points, the way an office suite offers them.
const QMarginsF NormalMargins(72, 72, 72, 72);
const QMarginsF NarrowMargins(36, 36, 36, 36);
const QMarginsF ModerateMargins(54, 72, 54, 72);
const QMarginsF WideMargins(144, 72, 144, 72);

/// @return the rectangle of a page of @p size inside @p canvas, keeping its
/// proportions.
QRectF fittedPage(const QSizeF &size, const QSizeF &canvas, qreal padding = 2.0)
{
    const qreal maxWidth = canvas.width() - 2 * padding;
    const qreal maxHeight = canvas.height() - 2 * padding;
    const qreal ratio = qBound(0.25, size.width() / qMax(1.0, size.height()), 4.0);
    qreal width = maxHeight * ratio;
    qreal height = maxHeight;
    if (width > maxWidth) {
        width = maxWidth;
        height = maxWidth / ratio;
    }
    return QRectF((canvas.width() - width) / 2.0, (canvas.height() - height) / 2.0, width, height);
}

/// Paint the sheet of paper of a page picture.
void paintPage(QPainter &painter, const QRectF &page, const QPalette &palette)
{
    painter.setPen(QPen(palette.color(QPalette::WindowText).lighter(150), 1.0));
    painter.setBrush(palette.color(QPalette::Base));
    painter.drawRoundedRect(page.adjusted(0.5, 0.5, -0.5, -0.5), 1.0, 1.0);
    painter.setBrush(Qt::NoBrush);
}

/// Paint lines of text inside @p area, so that a page reads as a page of text.
void paintTextLines(QPainter &painter, const QRectF &area, const QColor &color)
{
    QColor line = color;
    line.setAlpha(90);
    painter.setPen(QPen(line, 1.0));
    const qreal left = area.left() + 2.0;
    const qreal right = area.right() - 2.0;
    if (right - left < 3) {
        return;
    }
    bool first = true;
    for (qreal y = area.top() + 3.5; y < area.bottom() - 2; y += 4.0) {
        // The last line of a paragraph is short, as text is.
        painter.drawLine(QPointF(left, y), QPointF(first ? right : right - 2.0, y));
        first = false;
    }
}

/// @return the text area of a page drawn in @p page for @p margins. With
/// @p keepVisible the margins are drawn at least a tenth of the page wide, so
/// that a picture of them stays readable when they are small.
QRectF textArea(const QRectF &page, const QSizeF &size, const QMarginsF &margins, bool keepVisible = false)
{
    const qreal width = qMax(1.0, size.width());
    const qreal height = qMax(1.0, size.height());
    const qreal minimum = keepVisible ? 0.10 : 0.0;
    const auto fraction = [minimum](qreal margin, qreal length) {
        return qBound(minimum, margin / length, 0.4);
    };
    const qreal left = fraction(margins.left(), width) * page.width();
    const qreal right = fraction(margins.right(), width) * page.width();
    const qreal top = fraction(margins.top(), height) * page.height();
    const qreal bottom = fraction(margins.bottom(), height) * page.height();
    return page.adjusted(left, top, -right, -bottom);
}

/// @return a small icon of the break @p action inserts, for a menu entry.
QPixmap breakIcon(const QString &action, const QPalette &palette)
{
    const qreal ratio = 2.0;
    QPixmap pixmap(QSize(qRound(16 * ratio), qRound(16 * ratio)));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor line = palette.color(QPalette::WindowText);
    const QColor accent = palette.color(QPalette::Highlight);

    painter.setBrush(palette.color(QPalette::Base));
    painter.setPen(QPen(line, 1.0));
    if (action == QLatin1String("insert_framebreak")) {
        painter.drawRect(QRectF(3.5, 1.5, 9, 5));
        painter.drawRect(QRectF(3.5, 9.5, 9, 5));
        painter.setPen(QPen(accent, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(0.5, 8), QPointF(15.5, 8));
    } else if (action == QLatin1String("insert_section")) {
        painter.drawRect(QRectF(3.5, 1.5, 9, 13));
        painter.setPen(QPen(accent, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(4, 8), QPointF(12, 8));
    } else {
        painter.drawRect(QRectF(3.5, 1.5, 9, 13));
        QPainterPath hook;
        hook.moveTo(11.5, 11);
        hook.lineTo(8.5, 11);
        hook.lineTo(8.5, 7);
        painter.setPen(QPen(accent, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(hook);
        QPainterPath head;
        head.moveTo(7, 8);
        head.lineTo(8.5, 6);
        head.lineTo(10, 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawPath(head);
    }
    return pixmap;
}

/// Paint a page split into @p count text columns.
void paintColumns(QPainter &painter, const QRectF &area, int count, const QColor &color)
{
    const qreal gap = 2.0;
    const qreal columnWidth = (area.width() - gap * (count - 1)) / count;
    if (count <= 1 || columnWidth < 3) {
        paintTextLines(painter, area, color);
        return;
    }
    for (int column = 0; column < count; ++column) {
        const QRectF columnRect(area.left() + column * (columnWidth + gap), area.top(), columnWidth, area.height());
        paintTextLines(painter, columnRect, color);
    }
}
}

/**
 * @short One entry of the page setup group
 *
 * A picture of the page over its name, with the arrow that opens the presets of
 * the entry underneath, like the page setup gallery of an office suite.
 */
class KWPageSetupGallery::GalleryTile : public QWidget
{
public:
    GalleryTile(const QPixmap &preview, const QString &caption, QWidget *parent)
        : QWidget(parent)
        , m_preview(preview)
        , m_caption(caption)
    {
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        setToolTip(i18n("%1: choose a preset", caption));
    }

    std::function<void()> activated;

    void setPreview(const QPixmap &preview)
    {
        m_preview = preview;
        update();
    }

    QSize sizeHint() const override
    {
        return QSize(TileWidth, PreviewSize.height() + 4 + 2 * LineHeight + ArrowStripHeight + 5);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (m_hovered || m_pressed) {
            QColor fill = palette().color(QPalette::Highlight);
            fill.setAlpha(m_pressed ? 90 : 45);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
        }

        painter.drawPixmap((width() - PreviewSize.width()) / 2, 4, m_preview);

        QFont captionFont = font();
        captionFont.setPointSizeF(7.5);
        painter.setFont(captionFont);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(QRect(2, 4 + PreviewSize.height() + 2, width() - 4, LineHeight),
                         Qt::AlignHCenter | Qt::AlignTop,
                         m_caption);

        // The arrow of the presets, centred under the name.
        const qreal centerX = width() / 2.0;
        const qreal top = height() - ArrowStripHeight + 2;
        QPainterPath arrow;
        arrow.moveTo(centerX - 4, top);
        arrow.lineTo(centerX + 4, top);
        arrow.lineTo(centerX, top + 4);
        arrow.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::WindowText));
        painter.drawPath(arrow);
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_pressed = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !m_pressed) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        m_pressed = false;
        update();
        if (rect().contains(event->pos()) && activated) {
            activated();
        }
    }

private:
    QPixmap m_preview;
    QString m_caption;
    bool m_hovered = false;
    bool m_pressed = false;
};

KWPageSetupGallery::KWPageSetupGallery(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(i18n("Page Setup"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);

    addTile(PageSetupKind::Margins, i18n("Margins"),
            {{i18n("Normal"), QVariant::fromValue(NormalMargins)},
             {i18n("Narrow"), QVariant::fromValue(NarrowMargins)},
             {i18n("Moderate"), QVariant::fromValue(ModerateMargins)},
             {i18n("Wide"), QVariant::fromValue(WideMargins)},
             {i18n("Custom"), QVariant(), true}});
    addTile(PageSetupKind::Orientation, i18n("Orientation"),
            {{i18n("Portrait"), int(KoPageFormat::Portrait)},
             {i18n("Landscape"), int(KoPageFormat::Landscape)}});
    addTile(PageSetupKind::Size, i18n("Size"),
            {{KoPageFormat::name(KoPageFormat::IsoA4Size), int(KoPageFormat::IsoA4Size)},
             {KoPageFormat::name(KoPageFormat::IsoA5Size), int(KoPageFormat::IsoA5Size)},
             {KoPageFormat::name(KoPageFormat::IsoB5Size), int(KoPageFormat::IsoB5Size)},
             {KoPageFormat::name(KoPageFormat::UsLetterSize), int(KoPageFormat::UsLetterSize)},
             {KoPageFormat::name(KoPageFormat::UsLegalSize), int(KoPageFormat::UsLegalSize)},
             {KoPageFormat::name(KoPageFormat::IsoA3Size), int(KoPageFormat::IsoA3Size)},
             {i18n("More Sizes"), QVariant(), true}});
    addTile(PageSetupKind::Columns, i18n("Columns"),
            {{i18n("One"), 1}, {i18n("Two"), 2}, {i18n("Three"), 3}, {i18n("More Columns"), QVariant(), true}});
    addTile(PageSetupKind::Breaks, i18n("Breaks"),
            {{i18n("Page"), QStringLiteral("insert_framebreak")},
             {i18n("Section"), QStringLiteral("insert_section")},
             {i18n("Line"), QStringLiteral("line_break")}});

    layout->addStretch();
    updatePreviews();
}

void KWPageSetupGallery::addTile(PageSetupKind kind, const QString &caption, const QList<Variant> &variants)
{
    auto *tile = new GalleryTile(preview(kind, QVariant(), palette()), caption, this);
    tile->activated = [this, kind, variants, tile] { openVariants(kind, variants, tile); };
    m_tiles.append(tile);
    m_tileKinds.append(kind);
    m_tileVariants.append(variants);
    layout()->addWidget(tile);
}

void KWPageSetupGallery::setState(const PageSetupState &state)
{
    m_state = state;
    updatePreviews();
}

PageSetupState KWPageSetupGallery::state() const
{
    return m_state;
}

void KWPageSetupGallery::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The page the tab shows may have changed while it was hidden.
    Q_EMIT refreshRequested();
}

void KWPageSetupGallery::updatePreviews()
{
    for (int i = 0; i < m_tiles.count(); ++i) {
        m_tiles.at(i)->setPreview(preview(m_tileKinds.at(i), QVariant(), palette()));
    }
}

QPixmap KWPageSetupGallery::preview(PageSetupKind kind, const QVariant &value, const QPalette &palette, bool custom) const
{
    const qreal ratio = 2.0;
    QPixmap pixmap(QSize(qRound(PreviewSize.width() * ratio), qRound(PreviewSize.height() * ratio)));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const QColor accent = palette.color(QPalette::Highlight);
    const QColor text = palette.color(QPalette::WindowText);

    QSizeF pageSize = m_state.size;
    QMarginsF margins = m_state.margins;
    int columns = m_state.columns;

    switch (kind) {
    case PageSetupKind::Margins:
        if (value.canConvert<QMarginsF>()) {
            margins = value.value<QMarginsF>();
        }
        break;
    case PageSetupKind::Orientation: {
        const auto orientation = value.isValid() ? static_cast<KoPageFormat::Orientation>(value.toInt())
                                                 : m_state.orientation;
        const bool swap = (orientation == KoPageFormat::Landscape) != (m_state.orientation == KoPageFormat::Landscape);
        if (swap) {
            pageSize.transpose();
        }
        break;
    }
    case PageSetupKind::Size: {
        if (value.isValid()) {
            const auto format = static_cast<KoPageFormat::Format>(value.toInt());
            const bool landscape = m_state.orientation == KoPageFormat::Landscape;
            pageSize = QSizeF(KoPageFormat::width(format, landscape ? KoPageFormat::Landscape : KoPageFormat::Portrait),
                              KoPageFormat::height(format, landscape ? KoPageFormat::Landscape : KoPageFormat::Portrait));
        }
        break;
    }
    case PageSetupKind::Columns:
        if (value.isValid()) {
            columns = value.toInt();
        }
        break;
    case PageSetupKind::Breaks:
        break;
    }

    const QRectF page = fittedPage(pageSize, PreviewSize);
    paintPage(painter, page, palette);

    switch (kind) {
    case PageSetupKind::Margins: {
        const QRectF area = textArea(page, pageSize, margins, true);
        QColor dashed = accent;
        dashed.setAlpha(170);
        painter.setPen(QPen(dashed, 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(area);
        paintTextLines(painter, area.adjusted(1, 1, -1, -1), text);
        break;
    }
    case PageSetupKind::Orientation:
        paintTextLines(painter, textArea(page, pageSize, m_state.margins), text);
        break;
    case PageSetupKind::Size: {
        paintTextLines(painter, textArea(page, pageSize, m_state.margins), text);
        {
            // The paper name tells the sizes apart even when their proportions
            // are close.
            const auto format = value.isValid() ? static_cast<KoPageFormat::Format>(value.toInt()) : m_state.format;
            const QString name = KoPageFormat::name(format);
            QFont font = painter.font();
            font.setPointSizeF(7.0);
            painter.setFont(font);
            const QFontMetricsF metrics(font);
            const QRectF label(page.center().x() - metrics.horizontalAdvance(name) / 2 - 3,
                               page.center().y() - 6, metrics.horizontalAdvance(name) + 6, 12);
            painter.setPen(Qt::NoPen);
            painter.setBrush(palette.color(QPalette::Base));
            painter.drawRoundedRect(label, 2, 2);
            painter.setPen(text);
            painter.drawText(label, Qt::AlignCenter, name);
        }
        break;
    }
    case PageSetupKind::Columns:
        paintColumns(painter, textArea(page, pageSize, m_state.margins), columns, text);
        break;
    case PageSetupKind::Breaks: {
        // Each break is a page that shows where the text goes on.
        const QString action = value.toString();
        const QRectF area = textArea(page, pageSize, m_state.margins);
        QColor dashed = accent;
        dashed.setAlpha(170);
        if (action.isEmpty() || action == QLatin1String("insert_framebreak")) {
            // Two pages, one after the other.
            const QRectF second(page.left(), area.center().y() + 2, page.width(), area.height() / 2 - 2);
            paintPage(painter, second, palette);
            paintTextLines(painter, QRectF(area.left(), area.top(), area.width(), area.height() / 2 - 5), text);
            paintTextLines(painter, second.adjusted(2, 2, -2, -2), text);
            painter.setPen(QPen(dashed, 1.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(page.left(), area.center().y()), QPointF(page.right(), area.center().y()));
        } else if (action == QLatin1String("insert_section")) {
            // A page whose text stops at a section divider.
            paintTextLines(painter, QRectF(area.left(), area.top(), area.width(), area.height() / 2 - 4), text);
            painter.setPen(QPen(dashed, 1.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(page.left() + 1, area.center().y()), QPointF(page.right() - 1, area.center().y()));
            paintTextLines(painter, QRectF(area.left(), area.center().y() + 4, area.width(), area.height() / 2 - 4), text);
        } else {
            // A line that ends with the mark of a line break.
            paintTextLines(painter, area.adjusted(0, 0, 0, -6), text);
            const qreal y = area.bottom() - 4;
            painter.setPen(QPen(accent, 1.2));
            QPainterPath hook;
            hook.moveTo(area.right() - 1, y);
            hook.lineTo(area.right() - 6, y);
            hook.lineTo(area.right() - 6, y - 4);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(hook);
            QPainterPath head;
            head.moveTo(area.right() - 9, y - 5);
            head.lineTo(area.right() - 6, y - 1);
            head.lineTo(area.right() - 3, y - 5);
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawPath(head);
        }
        break;
    }
    }

    if (custom) {
        QFont font = painter.font();
        font.setPointSizeF(9.0);
        painter.setFont(font);
        painter.setPen(text);
        painter.drawText(page, Qt::AlignCenter, QStringLiteral("\u2026"));
    }

    return pixmap;
}

void KWPageSetupGallery::openVariants(PageSetupKind kind, const QList<Variant> &variants, const QWidget *anchor)
{
    auto *menu = new QMenu(const_cast<QWidget *>(anchor));
    menu->setAttribute(Qt::WA_DeleteOnClose, true);

    if (kind == PageSetupKind::Breaks) {
        // A break is an action, so its list stays a plain menu with small
        // icons instead of large pictures of the page.
        for (const Variant &variant : variants) {
            QAction *action = menu->addAction(QIcon(breakIcon(variant.value.toString(), palette())), variant.caption);
            connect(action, &QAction::triggered, this, [this, kind, variant] { chooseVariant(kind, variant); });
        }
        menu->popup(anchor->mapToGlobal(QPoint(anchor->width() / 2 - menu->sizeHint().width() / 2, anchor->height())));
        return;
    }

    // The presets of the other entries are a row of page pictures in a menu, the
    // way the page setup gallery of an office suite shows them.
    auto *row = new QWidget(menu);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(4, 4, 4, 4);
    rowLayout->setSpacing(2);
    for (const Variant &variant : variants) {
        auto *tile = new GalleryTile(preview(kind, variant.value, palette(), variant.custom), variant.caption, row);
        tile->activated = [this, kind, variant, menu] {
            menu->close();
            chooseVariant(kind, variant);
        };
        rowLayout->addWidget(tile);
    }

    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(row);
    menu->addAction(action);
    menu->popup(anchor->mapToGlobal(QPoint(anchor->width() / 2 - menu->sizeHint().width() / 2, anchor->height())));
}

void KWPageSetupGallery::chooseVariant(PageSetupKind kind, const Variant &variant)
{
    if (variant.custom) {
        Q_EMIT pageSetupRequested();
        return;
    }
    switch (kind) {
    case PageSetupKind::Margins:
        Q_EMIT marginsSelected(variant.value.value<QMarginsF>());
        break;
    case PageSetupKind::Orientation:
        Q_EMIT orientationSelected(static_cast<KoPageFormat::Orientation>(variant.value.toInt()));
        break;
    case PageSetupKind::Size:
        Q_EMIT pageFormatSelected(static_cast<KoPageFormat::Format>(variant.value.toInt()));
        break;
    case PageSetupKind::Columns:
        Q_EMIT columnsSelected(variant.value.toInt());
        break;
    case PageSetupKind::Breaks:
        Q_EMIT actionRequested(variant.value.toString());
        break;
    }
}
