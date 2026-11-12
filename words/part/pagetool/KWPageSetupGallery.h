/*
 *  SPDX-FileCopyrightText: 2026 Calligra developers
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KWPAGESETUPGALLERY_H
#define KWPAGESETUPGALLERY_H

#include <KoPageFormat.h>

#include <QMarginsF>
#include <QSizeF>
#include <QWidget>

/**
 * The settings the page setup gallery draws its previews from.
 */
struct PageSetupState {
    /// Size of one page, in points.
    QSizeF size = QSizeF(595, 842);
    /// The margins between the page edge and the text area, in points.
    QMarginsF margins = QMarginsF(72, 72, 72, 72);
    KoPageFormat::Format format = KoPageFormat::IsoA4Size;
    KoPageFormat::Orientation orientation = KoPageFormat::Portrait;
    /// Number of text columns on the page.
    int columns = 1;
};

/// The galleries of the page setup group.
enum class PageSetupKind {
    Margins,
    Orientation,
    Size,
    Columns,
    Breaks,
};

/**
 * @short The page setup gallery of the Layout ribbon tab
 *
 * Shows the page margins, the orientation, the paper size and the column
 * layout as large pictures of the page, the way the page setup group of an
 * office suite does. Every picture has an arrow that opens the presets of its
 * setting, each of them again drawn as a small page.
 */
class KWPageSetupGallery : public QWidget
{
    Q_OBJECT
public:
    explicit KWPageSetupGallery(QWidget *parent = nullptr);

    /// Reflect @p state in the four pictures.
    void setState(const PageSetupState &state);
    PageSetupState state() const;

Q_SIGNALS:
    /// The user picked margins, in points.
    void marginsSelected(const QMarginsF &margins);
    /// The user picked an orientation.
    void orientationSelected(KoPageFormat::Orientation orientation);
    /// The user picked a paper size.
    void pageFormatSelected(KoPageFormat::Format format);
    /// The user picked a number of text columns.
    void columnsSelected(int count);
    /// The user picked a break; @p actionName is the view action that inserts it.
    void actionRequested(const QString &actionName);
    /// The user asked for the page dialog, i.e. for margins that are not a preset.
    void pageSetupRequested();
    /// The gallery became visible and wants the current page setup again.
    void refreshRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct Variant {
        QString caption;
        QVariant value;
        bool custom = false;
    };

    /// One gallery entry: a picture of the page over its name.
    class GalleryTile;

    void addTile(PageSetupKind kind, const QString &caption, const QList<Variant> &variants);
    void updatePreviews();
    QPixmap preview(PageSetupKind kind, const QVariant &value, const QPalette &palette, bool custom = false) const;
    void openVariants(PageSetupKind kind, const QList<Variant> &variants, const QWidget *anchor);
    void chooseVariant(PageSetupKind kind, const Variant &variant);

    PageSetupState m_state;
    QList<GalleryTile *> m_tiles;
    QList<PageSetupKind> m_tileKinds;
    QList<QList<Variant>> m_tileVariants;
};

#endif // KWPAGESETUPGALLERY_H
