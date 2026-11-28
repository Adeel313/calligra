// This file is part of the KDE project
// SPDX-FileCopyrightText: 2010 Marijn Kruisselbrink <mkruisselbrink@kde.org>
// SPDX-License-Identifier: LGPL-2.0-only

#include "TestStyleStorage.h"

#include <core/Map.h>
#include <core/StyleStorage.h>
#include <core/StyleManager.h>

#include <QTest>

using namespace Calligra::Sheets;

class MyStyleStorage : public StyleStorage
{
public:
    MyStyleStorage(Map *map)
        : StyleStorage(map)
    {
    }
    using StyleStorage::garbageCollection;
};

void TestStyleStorage::testGarbageCollection()
{
    Map map;
    MyStyleStorage storage(&map);

    QRect rect(5, 5, 1, 1);
    QColor c1(Qt::red);
    QColor c2(Qt::blue);
    SharedSubStyle style1(new SubStyleOne<QColor>(Style::BackgroundColor, c1));
    SharedSubStyle style2(new SubStyleOne<QColor>(Style::BackgroundColor, c2));
    // we need to do this for multiple cells, so hopefully we'll end up with substyles that are for the same cell but not in the same leafnode in the rtree
    for (int i = 0; i < 100; i++)
        storage.insert(rect.adjusted(10 * i, 0, 10 * i, 0), style1);
    for (int i = 0; i < 100; i++)
        QCOMPARE(storage.contains(rect.adjusted(10 * i, 0, 10 * i, 0)).backgroundColor(), c1);
    for (int i = 0; i < 100; i++)
        storage.insert(rect.adjusted(10 * i, 0, 10 * i, 0), style2);
    for (int i = 0; i < 100; i++)
        QCOMPARE(storage.contains(rect.adjusted(10 * i, 0, 10 * i, 0)).backgroundColor(), c2);
    for (int i = 0; i < 100; i++)
        storage.insert(rect.adjusted(10 * i, 0, 10 * i, 0), style1);
    for (int i = 0; i < 100; i++)
        QCOMPARE(storage.contains(rect.adjusted(10 * i, 0, 10 * i, 0)).backgroundColor(), c1);
    for (int j = 0; j < 1000; j++) {
        storage.garbageCollection();
        for (int i = 0; i < 100; i++)
            QCOMPARE(storage.contains(rect.adjusted(10 * i, 0, 10 * i, 0)).backgroundColor(), c1);
    }
}

void TestStyleStorage::testCyclicParentStyles_data()
{
    QTest::addColumn<int>("cycle");
    QTest::newRow("acyclic") << 0;
    QTest::newRow("self-parent-ancestor") << 1;
    QTest::newRow("two-ancestor-cycle") << 2;
    QTest::newRow("cycle-back-to-child") << 3;
}

void TestStyleStorage::testCyclicParentStyles()
{
    QFETCH(int, cycle);
    Map map;
    StyleManager *manager = map.styleManager();
    auto *root = new CustomStyle("Root");
    root->setFontSize(17);
    manager->insertStyle(root);
    auto *parent = new CustomStyle("Parent", root);
    parent->setFontBold(true);
    manager->insertStyle(parent);
    auto *child = new CustomStyle("Child", parent);
    child->setBackgroundColor(Qt::blue);
    manager->insertStyle(child);
    if (cycle == 1)
        root->setParentName(root->name());
    else if (cycle == 2)
        root->setParentName(parent->name());
    else if (cycle == 3)
        root->setParentName(child->name());

    MyStyleStorage storage(&map);
    Style applied;
    applied.setParentName(child->name());
    storage.insert(Region(QRect(1, 1, 1, 1)), applied);
    const Style result = storage.contains(QPoint(1, 1));
    QCOMPARE(result.fontSize(), 17);
    QVERIFY(result.bold());
    QCOMPARE(result.backgroundColor(), QColor(Qt::blue));
}

QTEST_MAIN(TestStyleStorage)
