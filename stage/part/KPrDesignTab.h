/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Calligra developers
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef KPRDESIGNTAB_H
#define KPRDESIGNTAB_H

#include <QObject>
#include <QHash>
#include <QPointer>
#include <memory>
#include <QSharedPointer>

class QColor;
class QHBoxLayout;
class QMenu;
class QToolButton;
class QWidget;

class KPrView;
class KoPADocument;
class KoPAMasterPage;
class KoPAPage;
class KoShapeBackground;

namespace KPrThemeArtwork
{
struct Theme;
}

struct KPrSlideDesign {
    KoPAMasterPage *master = nullptr;
    QSharedPointer<KoShapeBackground> background;
    bool inheritsBackground = false;
    bool backgroundVisible = false;
    bool masterShapes = false;

    bool operator==(const KPrSlideDesign &) const = default;
};

class KPrDesignTab : public QObject
{
    Q_OBJECT
public:
    KPrDesignTab(KPrView *view, QWidget *page);

private Q_SLOTS:
    void rebuildThemes();

private:
    QWidget *createThemesGroup();
    QWidget *createSlideControls();
    QWidget *createCustomizeGroup();
    void captureOriginal(KoPADocument *document);
    void applyOriginal();
    void applyTheme(KoPAMasterPage *master);
    void applyTemplateTheme(const QString &file, const QString &name);
    void applyArtworkTheme(const KPrThemeArtwork::Theme &theme);
    void applyNewMaster(std::unique_ptr<KoPAMasterPage> master);
    void applyColor(const QColor &color);

    QHash<KoPAPage *, KPrSlideDesign> m_originalDesigns;
    KPrSlideDesign m_defaultOriginal;
    QPointer<KPrView> m_view;
    QWidget *m_page;
    QToolButton *m_themesButton;
    QMenu *m_themesMenu;
    QHBoxLayout *m_themeStrip;
};

#endif // KPRDESIGNTAB_H
