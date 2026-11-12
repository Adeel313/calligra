/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2007 Martin Pfeiffer <hubipete@gmx.net>
                 2009 Jeremias Epperlein <jeeree@web.de>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "FormulaRibbonWidgets.h"

#include "KoFormulaTool.h"

#include <KLocalizedString>
#include <QAction>
#include <QEnterEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <limits>

namespace
{
/// A compact symbolic icon size for the symbol palette
const int SymbolButtonSize = 28;
/// The symbol palette is two rows of this many glyph buttons
const int Columns = 10;
/// The preview of a structure is drawn as if on a canvas of this size; the
/// pixmap is rendered at twice that resolution to keep it crisp.
const QSize PreviewSize(54, 38);
/// The height of the strip with the arrow of a button that has variants.
const int ArrowStripHeight = 12;

/// @return the font the previews are painted with, a math font when the
/// system has one.
QFont mathFont(int pixelSize)
{
    QFont font;
    const QStringList preferred{QStringLiteral("Latin Modern Math"), QStringLiteral("Cambria Math"),
                                QStringLiteral("STIX Two Math"), QStringLiteral("DejaVu Math TeX Gyre"),
                                QStringLiteral("DejaVu Serif")};
    const QStringList families = QFontDatabase::families();
    for (const QString &family : preferred) {
        if (families.contains(family)) {
            font.setFamily(family);
            break;
        }
    }
    font.setPixelSize(pixelSize);
    return font;
}

/// Paint @p text inside @p rect with the math font, variables in italics.
void drawMathText(QPainter &painter, const QString &text, const QRectF &rect, int pixelSize,
                  Qt::Alignment alignment = Qt::AlignCenter, bool italic = true)
{
    QFont font = mathFont(pixelSize);
    font.setItalic(italic);
    painter.setFont(font);
    painter.drawText(rect, alignment, text);
}

/**
 * @return the picture a structure button shows, e.g. a small fraction for the
 * fraction template. The pictures are painted here instead of being taken from
 * the icon theme so that every template reads as the equation it inserts, as
 * the structure gallery of an office suite does. The picture is painted for a
 * canvas of PreviewSize; @p zoom enlarges it for a menu entry.
 */
QPixmap structurePreview(const QString &action, const QColor &color, qreal zoom = 1.0)
{
    const qreal ratio = 2.0;
    QPixmap pixmap(QSize(qRound(PreviewSize.width() * ratio * zoom), qRound(PreviewSize.height() * ratio * zoom)));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter.scale(zoom, zoom);
    painter.setPen(QPen(color, 1.0));

    if (action == QLatin1String("insert_fraction")) {
        drawMathText(painter, QStringLiteral("x"), QRectF(0, 0, PreviewSize.width(), 17), 16);
        painter.drawLine(QPointF(14, 19.5), QPointF(40, 19.5));
        drawMathText(painter, QStringLiteral("y"), QRectF(0, 21, PreviewSize.width(), 17), 16);
    } else if (action == QLatin1String("insert_bevelled_fraction")) {
        // A fraction whose bar is a slash, the way the skewed fraction of an
        // office suite looks.
        drawMathText(painter, QStringLiteral("x"), QRectF(26, 0, 22, 16), 14);
        drawMathText(painter, QStringLiteral("y"), QRectF(6, 22, 22, 16), 14);
        painter.drawLine(QPointF(14, 25), QPointF(38, 12));
    } else if (action == QLatin1String("insert_supscript")) {
        drawMathText(painter, QStringLiteral("e"), QRectF(3, 0, 28, 38), 25, Qt::AlignLeft | Qt::AlignBottom);
        drawMathText(painter, QStringLiteral("x"), QRectF(31, 2, 20, 18), 13, Qt::AlignLeft | Qt::AlignTop);
    } else if (action == QLatin1String("insert_subscript")) {
        drawMathText(painter, QStringLiteral("x"), QRectF(3, 0, 28, 26), 23, Qt::AlignLeft | Qt::AlignTop);
        drawMathText(painter, QStringLiteral("i"), QRectF(29, 18, 20, 20), 13, Qt::AlignLeft | Qt::AlignBottom);
    } else if (action == QLatin1String("insert_subsupscript")) {
        drawMathText(painter, QStringLiteral("x"), QRectF(3, 6, 26, 28), 22, Qt::AlignLeft | Qt::AlignVCenter);
        drawMathText(painter, QStringLiteral("j"), QRectF(28, 1, 20, 18), 12, Qt::AlignLeft | Qt::AlignTop);
        drawMathText(painter, QStringLiteral("i"), QRectF(28, 18, 20, 20), 12, Qt::AlignLeft | Qt::AlignBottom);
    } else if (action == QLatin1String("insert_sqrt") || action == QLatin1String("insert_root")) {
        if (action == QLatin1String("insert_root")) {
            drawMathText(painter, QStringLiteral("n"), QRectF(2, 0, 12, 11), 9);
        }
        // The radical sign is drawn as a path, so that it always meets the
        // line over the radicand.
        QPainterPath radical;
        radical.moveTo(2, 22);
        radical.lineTo(9, 35);
        radical.lineTo(16, 8);
        radical.lineTo(52, 8);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 1.2));
        painter.drawPath(radical);
        painter.setPen(QPen(color, 1.0));
        drawMathText(painter, QStringLiteral("x"), QRectF(22, 9, 26, 20), 15);
    } else if (action == QLatin1String("insert_integral")) {
        drawMathText(painter, QStringLiteral("\u222B"), QRectF(2, 0, 26, 38), 30, Qt::AlignLeft | Qt::AlignBottom, false);
        drawMathText(painter, QStringLiteral("x"), QRectF(24, 3, 18, 15), 11, Qt::AlignLeft | Qt::AlignTop);
        drawMathText(painter, QStringLiteral("-x"), QRectF(24, 22, 26, 15), 11, Qt::AlignLeft | Qt::AlignBottom);
    } else if (action == QLatin1String("insert_large_operator")) {
        drawMathText(painter, QStringLiteral("n"), QRectF(0, 0, PreviewSize.width(), 11), 9);
        drawMathText(painter, QStringLiteral("\u2211"), QRectF(0, 9, PreviewSize.width(), 22), 22, Qt::AlignCenter, false);
        drawMathText(painter, QStringLiteral("i=0"), QRectF(0, 28, PreviewSize.width(), 10), 8, Qt::AlignCenter, false);
    } else if (action == QLatin1String("insert_function")) {
        drawMathText(painter, QStringLiteral("sin"), QRectF(0, 8, 30, 20), 14, Qt::AlignLeft | Qt::AlignVCenter, false);
        drawMathText(painter, QStringLiteral("\u03B8"), QRectF(30, 6, 22, 22), 17, Qt::AlignLeft | Qt::AlignVCenter);
    } else if (action == QLatin1String("insert_accent")) {
        drawMathText(painter, QStringLiteral("a"), QRectF(0, 8, PreviewSize.width(), 28), 22, Qt::AlignCenter);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(PreviewSize.width() / 2.0 - 2, 7), 1.6, 1.6);
        painter.drawEllipse(QPointF(PreviewSize.width() / 2.0 + 2, 7), 1.6, 1.6);
        painter.setPen(QPen(color, 1.0));
    } else if (action == QLatin1String("insert_operator")) {
        drawMathText(painter, QStringLiteral("\u0394"), QRectF(0, 8, PreviewSize.width(), 30), 24, Qt::AlignCenter, false);
        painter.drawLine(QPointF(16, 37), QPointF(38, 37));
    } else if (action == QLatin1String("insert_fence")) {
        drawMathText(painter, QStringLiteral("( )"), QRectF(0, 0, PreviewSize.width(), PreviewSize.height()), 24,
                     Qt::AlignCenter, false);
    } else if (action == QLatin1String("insert_enclosed")) {
        drawMathText(painter, QStringLiteral("x"), QRectF(0, 4, PreviewSize.width(), 26), 15);
        painter.drawRect(QRectF(11, 7, 32, 24));
    } else if (action == QLatin1String("insert_21table")) {
        // A column vector: a bracket around two cells.
        drawMathText(painter, QStringLiteral("["), QRectF(14, 3, 10, 32), 24, Qt::AlignLeft | Qt::AlignVCenter, false);
        drawMathText(painter, QStringLiteral("]"), QRectF(38, 3, 10, 32), 24, Qt::AlignRight | Qt::AlignVCenter, false);
        drawMathText(painter, QStringLiteral("x"), QRectF(24, 6, 12, 12), 10);
        drawMathText(painter, QStringLiteral("y"), QRectF(24, 21, 12, 12), 10);
    } else if (action.startsWith(QLatin1String("insert_")) && action.endsWith(QLatin1String("table"))) {
        drawMathText(painter, QStringLiteral("["), QRectF(3, 3, 12, 32), 26, Qt::AlignLeft | Qt::AlignVCenter, false);
        drawMathText(painter, QStringLiteral("]"), QRectF(39, 3, 12, 32), 26, Qt::AlignRight | Qt::AlignVCenter, false);
        drawMathText(painter, QStringLiteral("1"), QRectF(15, 6, 11, 13), 11, Qt::AlignCenter, false);
        drawMathText(painter, QStringLiteral("0"), QRectF(28, 6, 11, 13), 11, Qt::AlignCenter, false);
        drawMathText(painter, QStringLiteral("0"), QRectF(15, 20, 11, 13), 11, Qt::AlignCenter, false);
        drawMathText(painter, QStringLiteral("1"), QRectF(28, 20, 11, 13), 11, Qt::AlignCenter, false);
    } else if (action == QLatin1String("insert_underscript")) {
        drawMathText(painter, QStringLiteral("lim"), QRectF(0, 4, PreviewSize.width(), 18), 15, Qt::AlignCenter, false);
        drawMathText(painter, QStringLiteral("n\u2192\u221E"), QRectF(0, 21, PreviewSize.width(), 14), 10, Qt::AlignCenter,
                     false);
    } else if (action == QLatin1String("insert_overscript")) {
        drawMathText(painter, QStringLiteral("n\u2192\u221E"), QRectF(0, 1, PreviewSize.width(), 12), 10, Qt::AlignCenter,
                     false);
        drawMathText(painter, QStringLiteral("lim"), QRectF(0, 14, PreviewSize.width(), 20), 15, Qt::AlignCenter, false);
    } else if (action == QLatin1String("insert_underoverscript")) {
        drawMathText(painter, QStringLiteral("n\u2192\u221E"), QRectF(0, 0, PreviewSize.width(), 11), 9, Qt::AlignCenter,
                     false);
        drawMathText(painter, QStringLiteral("lim"), QRectF(0, 11, PreviewSize.width(), 18), 14, Qt::AlignCenter, false);
        drawMathText(painter, QStringLiteral("i=0"), QRectF(0, 28, PreviewSize.width(), 10), 9, Qt::AlignCenter, false);
    } else {
        drawMathText(painter, QStringLiteral("f(x)"), QRectF(0, 0, PreviewSize.width(), PreviewSize.height()), 13);
    }
    return pixmap;
}

/// @return @p text broken into at most two lines that fit @p maxWidth.
QStringList wrapToLines(const QString &text, const QFontMetrics &metrics, int maxWidth)
{
    if (metrics.horizontalAdvance(text) <= maxWidth) {
        return {text};
    }
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.count() < 2) {
        return {text};
    }
    // Split at the word boundary that leaves the two lines most even.
    int bestSplit = 1;
    int bestWidth = std::numeric_limits<int>::max();
    for (int split = 1; split < words.count(); ++split) {
        const int first = metrics.horizontalAdvance(words.mid(0, split).join(QLatin1Char(' ')));
        const int second = metrics.horizontalAdvance(words.mid(split).join(QLatin1Char(' ')));
        const int width = qMax(first, second);
        if (width < bestWidth) {
            bestWidth = width;
            bestSplit = split;
        }
    }
    return {words.mid(0, bestSplit).join(QLatin1Char(' ')), words.mid(bestSplit).join(QLatin1Char(' '))};
}

/// @return the name of a template action without the "Insert" of its text.
QString variantCaption(const QString &text)
{
    QString caption = text;
    if (caption.startsWith(QLatin1String("Insert "), Qt::CaseInsensitive)) {
        caption.remove(0, 7);
    }
    if (!caption.isEmpty()) {
        caption[0] = caption.at(0).toUpper();
    }
    return caption;
}

/**
 * @short One variant in the menu of a structure button
 *
 * Shows the picture of the variant over its name, the way the entries of the
 * equation gallery of an office suite look.
 */
class FormulaVariantTile : public QWidget
{
public:
    FormulaVariantTile(const QPixmap &preview, const QString &caption, QWidget *parent)
        : QWidget(parent)
        , m_preview(preview)
        , m_caption(caption)
    {
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(caption);
        QFont captionFont = font();
        captionFont.setPointSizeF(8.0);
        m_lines = wrapToLines(caption, QFontMetrics(captionFont), MaxCaptionWidth);
    }

    std::function<void()> clicked;

    QSize sizeHint() const override
    {
        const QSize preview = m_preview.deviceIndependentSize().toSize();
        return QSize(qMax(TileMinWidth, qMax(preview.width(), lineWidth()) + 16),
                     preview.height() + 6 + m_lines.count() * LineHeight + 8);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (m_hovered) {
            QColor fill = palette().color(QPalette::Highlight);
            fill.setAlpha(45);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
        }

        const QSize preview = m_preview.deviceIndependentSize().toSize();
        painter.drawPixmap((width() - preview.width()) / 2, 5, m_preview);

        QFont captionFont = font();
        captionFont.setPointSizeF(8.0);
        painter.setFont(captionFont);
        painter.setPen(palette().color(QPalette::WindowText));
        int lineTop = 5 + preview.height() + 2;
        for (const QString &line : std::as_const(m_lines)) {
            painter.drawText(QRect(2, lineTop, width() - 4, LineHeight), Qt::AlignHCenter | Qt::AlignTop, line);
            lineTop += LineHeight;
        }
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

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && clicked) {
            clicked();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    static constexpr int LineHeight = 13;
    static constexpr int TileMinWidth = 92;
    static constexpr int MaxCaptionWidth = 110;

    int lineWidth() const
    {
        QFont captionFont = font();
        captionFont.setPointSizeF(8.0);
        const QFontMetrics metrics(captionFont);
        int width = 0;
        for (const QString &line : m_lines) {
            width = qMax(width, metrics.horizontalAdvance(line));
        }
        return width;
    }

    QPixmap m_preview;
    QString m_caption;
    QStringList m_lines;
    bool m_hovered = false;
};

/**
 * @short The menu with the variants of a structure
 *
 * A popup of preview tiles instead of a plain list of actions, so that the
 * variants of a structure can be told apart by their picture, as in the
 * equation gallery of an office suite.
 */
class FormulaVariantsPopup : public QFrame
{
public:
    explicit FormulaVariantsPopup(QWidget *parent)
        : QFrame(parent, Qt::Popup)
    {
        setFrameShape(QFrame::StyledPanel);
        setAttribute(Qt::WA_DeleteOnClose, true);
        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(6, 6, 6, 6);
        m_layout->setSpacing(4);
    }

    void addVariant(const QString &name, QAction *action, const QColor &color)
    {
        auto *tile = new FormulaVariantTile(structurePreview(name, color, 1.3), variantCaption(action->text()), this);
        tile->clicked = [this, action] {
            close();
            action->trigger();
        };
        m_layout->addWidget(tile);
    }

    /// Show the menu with its top edge centred under @p globalBottom.
    void popupUnder(const QPoint &globalBottom)
    {
        adjustSize();
        QPoint position = globalBottom - QPoint(width() / 2, 0);
        const QScreen *screen = QGuiApplication::screenAt(globalBottom);
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen) {
            const QRect area = screen->availableGeometry();
            position.setX(qBound(area.left() + 4, position.x(), area.right() - width() - 4));
            position.setY(qBound(area.top() + 4, position.y(), area.bottom() - height() - 4));
        }
        move(position);
        show();
    }

private:
    QHBoxLayout *m_layout;
};

/**
 * @short A structure button of the formula ribbon
 *
 * Shows the picture of the structure over its name, with the arrow that opens
 * the variants of the structure underneath the name, like the equation
 * gallery of an office suite.
 */
class FormulaTemplateButton : public QWidget
{
public:
    FormulaTemplateButton(const QPixmap &preview, const QString &caption, QWidget *parent)
        : QWidget(parent)
        , m_preview(preview)
        , m_caption(caption)
    {
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        m_lines = wrapCaption(caption);
    }

    /// The variants the arrow opens, each with the name of its template.
    void setVariants(const QList<QPair<QString, QAction *>> &variants)
    {
        m_variants = variants;
        update();
    }

    std::function<void()> clicked;

    QSize sizeHint() const override
    {
        // Every button reserves the room of a two line name, so that the
        // pictures sit on a line and the arrows of the variants line up.
        return QSize(qMax(PreviewSize.width(), lineWidth() + 4) + 8,
                     PreviewSize.height() + 4 + 2 * LineHeight + ArrowStripHeight + 5);
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

        const int previewTop = 3;
        painter.drawPixmap((width() - PreviewSize.width()) / 2, previewTop, m_preview);

        QFont captionFont = font();
        captionFont.setPointSizeF(7.5);
        painter.setFont(captionFont);
        painter.setPen(palette().color(QPalette::WindowText));
        int lineTop = previewTop + PreviewSize.height() + 2;
        for (const QString &line : std::as_const(m_lines)) {
            painter.drawText(QRect(2, lineTop, width() - 4, LineHeight), Qt::AlignHCenter | Qt::AlignTop, line);
            lineTop += LineHeight;
        }

        if (!m_variants.isEmpty()) {
            // The arrow of the variants, centred under the name.
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
        if (!m_variants.isEmpty() && event->pos().y() >= height() - ArrowStripHeight) {
            showVariants();
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
        if (rect().contains(event->pos()) && clicked) {
            clicked();
        }
    }

private:
    /// The height of one line of the caption.
    static const int LineHeight = 13;

    /// @return the caption broken into at most two lines, so that a long name
    /// like "Large Operator" does not make the button wide.
    QStringList wrapCaption(const QString &caption) const
    {
        QFont captionFont = font();
        captionFont.setPointSizeF(7.5);
        return wrapToLines(caption, QFontMetrics(captionFont), PreviewSize.width());
    }

    /// Pop up the menu with the variants of the structure.
    void showVariants()
    {
        const QColor color = palette().color(QPalette::WindowText);
        auto *popup = new FormulaVariantsPopup(this);
        for (const auto &[name, action] : m_variants) {
            popup->addVariant(name, action, color);
        }
        popup->popupUnder(mapToGlobal(QPoint(width() / 2, height())));
    }

    int lineWidth() const
    {
        QFont captionFont = font();
        captionFont.setPointSizeF(7.5);
        const QFontMetrics metrics(captionFont);
        int width = 0;
        for (const QString &line : m_lines) {
            width = qMax(width, metrics.horizontalAdvance(line));
        }
        return width;
    }

    QPixmap m_preview;
    QString m_caption;
    QStringList m_lines;
    QList<QPair<QString, QAction *>> m_variants;
    bool m_hovered = false;
    bool m_pressed = false;
};
}

FormulaStructuresWidget::FormulaStructuresWidget(KoFormulaTool *tool, QWidget *parent)
    : QWidget(parent)
    , m_tool(tool)
{
    setWindowTitle(i18n("Structures"));
    // Fill the width of the tab instead of hugging its controls.
    setProperty("ribbonExpandingGroup", true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    // The stretch between the buttons spaces them out; the buttons themselves
    // only need a few pixels of breathing room.
    layout->setSpacing(6);

    // The gallery follows the order of the equation ribbon of an office
    // suite: fraction, scripts, radical, then the operators and the remaining
    // templates.
    addTemplate(layout, i18n("Fraction"), QStringLiteral("insert_fraction"), {QStringLiteral("insert_bevelled_fraction")});
    addTemplate(layout, i18n("Script"), QStringLiteral("insert_supscript"),
                {QStringLiteral("insert_subscript"), QStringLiteral("insert_subsupscript")});
    addTemplate(layout, i18n("Radical"), QStringLiteral("insert_sqrt"), {QStringLiteral("insert_root")});
    addTemplate(layout, i18n("Integral"), QStringLiteral("insert_integral"));
    addTemplate(layout, i18n("Large Operator"), QStringLiteral("insert_large_operator"));
    addTemplate(layout, i18n("Bracket"), QStringLiteral("insert_fence"), {QStringLiteral("insert_enclosed")});
    addTemplate(layout, i18n("Function"), QStringLiteral("insert_function"));
    addTemplate(layout, i18n("Accent"), QStringLiteral("insert_accent"));
    addTemplate(layout, i18n("Limit and Log"), QStringLiteral("insert_underscript"),
                {QStringLiteral("insert_overscript"), QStringLiteral("insert_underoverscript")});
    addTemplate(layout, i18n("Operator"), QStringLiteral("insert_operator"));
    addTemplate(layout, i18n("Matrix"), QStringLiteral("insert_33table"), {QStringLiteral("insert_21table")});

    layout->addStretch();
}

void FormulaStructuresWidget::addTemplate(QHBoxLayout *layout, const QString &caption, const QString &action,
                                          const QStringList &variants)
{
    QAction *templateAction = m_tool->action(action);
    if (!templateAction) {
        return;
    }

    auto *button = new FormulaTemplateButton(structurePreview(action, palette().color(QPalette::WindowText)), caption, this);
    button->setToolTip(QStringLiteral("%1: %2").arg(caption, templateAction->text()));
    button->clicked = [templateAction] { templateAction->trigger(); };

    if (!variants.isEmpty()) {
        // The arrow opens the variants of the structure; the structure itself
        // is one of them.
        QList<QPair<QString, QAction *>> entries{{action, templateAction}};
        for (const QString &variant : variants) {
            if (QAction *variantAction = m_tool->action(variant)) {
                entries.append({variant, variantAction});
            }
        }
        if (entries.count() > 1) {
            button->setVariants(entries);
        }
    }

    // The buttons share the free width of the group instead of sitting in a
    // row at its left edge.
    layout->addStretch(1);
    layout->addWidget(button);
}

FormulaSymbolsWidget::FormulaSymbolsWidget(KoFormulaTool *tool, QWidget *parent)
    : QWidget(parent)
    , m_tool(tool)
    , m_grid(nullptr)
    , m_page(0)
{
    setWindowTitle(i18n("Symbols"));

    m_pages = {
        {QChar(0x00B1), QChar(0x221E), QChar(0x003D), QChar(0x2260), QChar(0x223C), QChar(0x00D7), QChar(0x00F7), QChar(0x221D), QChar(0x2264),
         QChar(0x2265), QChar(0x226A), QChar(0x226B), QChar(0x2248), QChar(0x2245), QChar(0x2208), QChar(0x2209), QChar(0x2205), QChar(0x2200),
         QChar(0x2203), QChar(0x00AC)},
        {QChar(0x222A), QChar(0x2229), QChar(0x2282), QChar(0x2283), QChar(0x2286), QChar(0x2287), QChar(0x2227), QChar(0x2228), QChar(0x21D2),
         QChar(0x21D4), QChar(0x2202), QChar(0x2207), QChar(0x2211), QChar(0x220F), QChar(0x222B), QChar(0x222E), QChar(0x221A), QChar(0x210F),
         QChar(0x2234), QChar(0x2235)},
        {QChar(0x03B1), QChar(0x03B2), QChar(0x03B3), QChar(0x03B4), QChar(0x03B5), QChar(0x03B6), QChar(0x03B7), QChar(0x03B8), QChar(0x03B9),
         QChar(0x03BA), QChar(0x03BB), QChar(0x03BC), QChar(0x03BD), QChar(0x03BE), QChar(0x03C0), QChar(0x03C1), QChar(0x03C3), QChar(0x03C4),
         QChar(0x03C6), QChar(0x0394)},
        {QChar(0x2190), QChar(0x2192), QChar(0x2191), QChar(0x2193), QChar(0x2194), QChar(0x21D0), QChar(0x21D4), QChar(0x21D2), QChar(0x2195),
         QChar(0x21C4), QChar(0x21A6), QChar(0x21A9), QChar(0x21AA), QChar(0x2196), QChar(0x2197), QChar(0x2198), QChar(0x2199), QChar(0x21A0),
         QChar(0x21DD), QChar(0x21C0)},
        {QChar(0x2220), QChar(0x22A5), QChar(0x2225), QChar(0x2308), QChar(0x2309), QChar(0x230A), QChar(0x230B), QChar(0x2026), QChar(0x22EF),
         QChar(0x2218), QChar(0x2295), QChar(0x2297), QChar(0x2299), QChar(0x2261), QChar(0x2262), QChar(0x2243), QChar(0x2244), QChar(0x22C6),
         QChar(0x211C), QChar(0x2111)},
    };

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    auto *palette = new QWidget(this);
    m_grid = new QGridLayout(palette);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(2);
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < Columns; ++column) {
            auto *button = new QToolButton(palette);
            button->setAutoRaise(false);
            button->setFixedSize(SymbolButtonSize, SymbolButtonSize);
            QFont glyphFont = button->font();
            glyphFont.setPointSizeF(12.0);
            button->setFont(glyphFont);
            connect(button, &QToolButton::clicked, this, [this, button] {
                m_tool->insertSymbol(button->text());
            });
            m_grid->addWidget(button, row, column);
            m_symbolButtons.append(button);
        }
    }
    layout->addWidget(palette);

    auto *pager = new QVBoxLayout;
    pager->setSpacing(2);
    auto *previous = new QToolButton(this);
    previous->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    previous->setAutoRaise(false);
    previous->setToolTip(i18n("Previous symbols"));
    auto *next = new QToolButton(this);
    next->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    next->setAutoRaise(false);
    next->setToolTip(i18n("More symbols"));
    connect(previous, &QToolButton::clicked, this, [this] {
        m_page = (m_page + m_pages.count() - 1) % m_pages.count();
        populatePage();
    });
    connect(next, &QToolButton::clicked, this, [this] {
        m_page = (m_page + 1) % m_pages.count();
        populatePage();
    });
    pager->addStretch();
    pager->addWidget(previous);
    pager->addWidget(next);
    pager->addStretch();
    layout->addLayout(pager);
    layout->addStretch();

    populatePage();
}

void FormulaSymbolsWidget::populatePage()
{
    const QList<QString> &symbols = m_pages.at(m_page);
    for (int i = 0; i < m_symbolButtons.count(); ++i) {
        QToolButton *button = m_symbolButtons.at(i);
        const bool hasSymbol = i < symbols.count();
        button->setVisible(hasSymbol);
        button->setText(hasSymbol ? symbols.at(i) : QString());
    }
}
