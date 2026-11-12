/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include <KoRibbonLayout.h>
#include <KoToolBase.h>

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QTest>
#include <QVBoxLayout>
#include <QWidgetAction>

class LegacyRibbonTool : public KoToolBase
{
public:
    LegacyRibbonTool() : KoToolBase(nullptr) {}
    void paint(QPainter &, const KoViewConverter &) override {}
    void mousePressEvent(KoPointerEvent *) override {}
    void mouseMoveEvent(KoPointerEvent *) override {}
    void mouseReleaseEvent(KoPointerEvent *) override {}
    void activate(ToolActivation, const QSet<KoShape *> &) override {}

protected:
    QWidget *createOptionWidget() override
    {
        auto *panel = new QWidget;
        panel->setWindowTitle(QStringLiteral("Legacy Options"));
        panel->setMinimumHeight(180);
        auto *layout = new QVBoxLayout(panel);
        auto *field = new QLineEdit(panel);
        field->setObjectName(QStringLiteral("retainedField"));
        layout->addWidget(field);
        return panel;
    }
};

class KoRibbonLayoutTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void actionsSurviveArrangement()
    {
        QWidget original;
        auto *originalLayout = new QVBoxLayout(&original);
        QAction command(QStringLiteral("Command"), &original);
        command.setCheckable(true);
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::blue);
        command.setIcon(QIcon(pixmap));
        auto *button = new QToolButton(&original);
        button->setDefaultAction(&command);
        auto *menu = new QMenu(button);
        menu->addAction(&command);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::MenuButtonPopup);
        originalLayout->addWidget(button);

        QWidget ribbon;
        KoRibbonLayout::arrange(&ribbon, {button}, {});
        QCOMPARE(button->defaultAction(), &command);
        QCOMPARE(button->menu(), menu);
        QCOMPARE(button->popupMode(), QToolButton::MenuButtonPopup);
        button->click();
        QVERIFY(command.isChecked());
        command.setEnabled(false);
        QVERIFY(!button->isEnabled());
        QVERIFY(button->property("ribbonCompactable").toBool());
        QCOMPARE(button->property("ribbonPresentationStyle").toInt(), int(Qt::ToolButtonTextUnderIcon));
    }

    void textOnlyCommandsKeepLabels()
    {
        QToolButton button;
        button.setText(QStringLiteral("Options"));
        KoRibbonLayout::configureButton(&button, Qt::ToolButtonTextBesideIcon);
        QVERIFY(!button.property("ribbonCompactable").toBool());
        QCOMPARE(button.accessibleName(), QStringLiteral("Options"));
    }

    void compactButtonsRestoreCustomGeometry()
    {
        QToolButton button;
        QPixmap pixmap(40, 40);
        pixmap.fill(Qt::blue);
        button.setIcon(QIcon(pixmap));
        button.setText(QStringLiteral("Custom Gallery"));
        KoRibbonLayout::configureButton(&button, Qt::ToolButtonTextUnderIcon);
        button.setIconSize(QSize(40, 40));
        button.setFixedSize(140, 90);
        const QSize minimum = button.minimumSize();
        const QSize maximum = button.maximumSize();
        for (int cycle = 0; cycle < 3; ++cycle) {
            KoRibbonLayout::setButtonCompact(&button, true);
            QCOMPARE(button.toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(button.minimumWidth() < minimum.width());
            QCOMPARE(button.minimumHeight(), minimum.height());
            KoRibbonLayout::setButtonCompact(&button, false);
            QCOMPARE(button.toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
            QCOMPARE(button.iconSize(), QSize(40, 40));
            QCOMPARE(button.minimumSize(), minimum);
            QCOMPARE(button.maximumSize(), maximum);
        }
    }

    void popupControlsKeepTheirOwnLayout()
    {
        QWidget page;
        auto *main = new QToolButton(&page);
        auto *menu = new QMenu(main);
        main->setMenu(menu);
        auto *panel = new QWidget;
        auto *popupButton = new QToolButton(panel);
        popupButton->setText(QStringLiteral("Popup control"));
        auto *action = new QWidgetAction(menu);
        action->setDefaultWidget(panel);
        menu->addAction(action);
        const auto buttons = KoRibbonLayout::pageButtons(&page);
        QVERIFY(buttons.contains(main));
        QVERIFY(!buttons.contains(popupButton));
    }

    void explicitlyHiddenControlsAreNotCompacted()
    {
        QWidget page;
        auto *hiddenPanel = new QWidget(&page);
        auto *button = new QToolButton(hiddenPanel);
        hiddenPanel->hide();
        QVERIFY(!KoRibbonLayout::pageButtons(&page).contains(button));
    }

    void otherApplicationsKeepNativePanels()
    {
        const QString previousName = qApp->applicationName();
        qApp->setApplicationName(QStringLiteral("calligrakarbon"));
        LegacyRibbonTool tool;
        QWidget *panel = tool.optionWidgets().first();
        QVERIFY(!panel->findChild<QToolButton *>());
        QVERIFY(panel->minimumHeight() >= 180);
        qApp->setApplicationName(previousName);
    }

    void legacyPanelsAreRetainedAndCached()
    {
        const QString previousName = qApp->applicationName();
        qApp->setApplicationName(QStringLiteral("calligrawords"));
        QPointer<QLineEdit> field;
        {
            LegacyRibbonTool tool;
            const auto options = tool.optionWidgets();
            QCOMPARE(options.size(), 1);
            QWidget *group = options.first();
            auto *button = group->findChild<QToolButton *>();
            QVERIFY(button);
            QVERIFY(button->menu());
            field = group->findChild<QLineEdit *>(QStringLiteral("retainedField"));
            QVERIFY(field);
            field->setText(QStringLiteral("Preserved"));
            QCOMPARE(tool.optionWidgets().first(), options.first());
            QCOMPARE(field->text(), QStringLiteral("Preserved"));
            QVERIFY(group->sizeHint().height() < 180);
        }
        QVERIFY(field.isNull());
        qApp->setApplicationName(previousName);
    }
};

QTEST_MAIN(KoRibbonLayoutTest)
#include "KoRibbonLayoutTest.moc"
