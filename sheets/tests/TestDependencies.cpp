// This file is part of the KDE project
// SPDX-FileCopyrightText: 2007 Stefan Nikolaus <stefan.nikolaus@kdemail.net>
// SPDX-License-Identifier: LGPL-2.0-only

#include "TestDependencies.h"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QTest>

#include "engine/CellBase.h"
#include "engine/CellBaseStorage.h"
#include "engine/DependencyManager.h"
#include "engine/DependencyManager_p.h"
#include "engine/Formula.h"
#include "engine/MapBase.h"
#include "engine/SheetBase.h"
#include "engine/Value.h"

using namespace Calligra::Sheets;

void TestDependencies::initTestCase()
{
    KLocalizedString::setApplicationDomain("calligrasheets");
    m_map = new MapBase;
    m_sheet = m_map->addNewSheet();
    m_sheet->setSheetName("Sheet1");
    m_storage = m_sheet->cellStorage();
}

void TestDependencies::testCircleRemoval()
{
    Formula formula(m_sheet);
    formula.setExpression("=A1");
    m_storage->setFormula(1, 1, formula); // A1

    QCoreApplication::processEvents(); // handle Damages

    QCOMPARE(m_storage->value(1, 1), Value::errorCIRCLE());
    DependencyManager *manager = m_map->dependencyManager();
    QVERIFY(manager->d->consumers.count() == 1);
    QVERIFY(manager->d->providers.count() == 1);
    QList<CellBase> consumers = manager->d->consumers.value(m_sheet)->contains(QRect(1, 1, 1, 1));
    QCOMPARE(consumers.count(), 1);
    QCOMPARE(consumers.first(), CellBase(m_sheet, 1, 1));
    QCOMPARE(manager->d->providers.value(CellBase(m_sheet, 1, 1)), Region(QRect(1, 1, 1, 1), m_sheet));

    m_storage->setFormula(1, 1, Formula()); // A1

    QCoreApplication::processEvents(); // handle Damages

    QCOMPARE(m_storage->value(1, 1), Value());
    QVERIFY(manager->d->consumers.value(m_sheet)->contains(QRect(1, 1, 1, 1)).count() == 0);
    QVERIFY(manager->d->providers.count() == 0);
}

void TestDependencies::testCircles()
{
    Formula formula(m_sheet);
    formula.setExpression("=A3");
    m_storage->setFormula(1, 1, formula); // A1
    formula.setExpression("=A1");
    m_storage->setFormula(1, 2, formula); // A2
    formula.setExpression("=A2");
    m_storage->setFormula(1, 3, formula); // A3

    QCoreApplication::processEvents(); // handle Damages

    QCOMPARE(m_storage->value(1, 1), Value::errorCIRCLE());
    QCOMPARE(m_storage->value(1, 2), Value::errorCIRCLE());
    QCOMPARE(m_storage->value(1, 3), Value::errorCIRCLE());
}

void TestDependencies::testSharedDependencyDepths()
{
    MapBase map;
    SheetBase *sheet = map.addNewSheet();
    auto *manager = map.dependencyManager();
    constexpr int count = 12;
    // A forward chain with two shared branches per cell must visit each
    // provider once, rather than recursively expanding every possible path.
    for (int row = 1; row <= count; ++row) {
        manager->d->providers.insert(CellBase(sheet, 1, row), Region(QRect(1, row + 1, 1, 2), sheet));
    }
    QCOMPARE(manager->d->computeDepth(CellBase(sheet, 1, 1)), count);
    QCOMPARE(manager->depths().size(), count);
    for (int row = 1; row <= count; ++row) {
        QCOMPARE(manager->depths().value(CellBase(sheet, 1, row)), count - row + 1);
    }
}

void TestDependencies::testSparseColumnDepth()
{
    MapBase map;
    SheetBase *sheet = map.addNewSheet();
    CellBase(sheet, 1, 100).setUserInput("=1");
    CellBase(sheet, 1, 101).setUserInput("=A100");
    CellBase(sheet, 1, 201).setUserInput("=A101");
    const CellBase consumer(sheet, 2, 1);
    CellBase(consumer).setUserInput("=SUM(A1:A200)");

    auto *manager = map.dependencyManager();
    manager->updateAllDependencies(&map);
    QCOMPARE(manager->depths().value(consumer), 2);
}

void TestDependencies::testDepths()
{
    CellBase a1(m_sheet, 1, 1);
    a1.parseUserInput("5");
    CellBase a2(m_sheet, 1, 2);
    a2.parseUserInput("=A1");
    CellBase a3(m_sheet, 1, 3);
    a3.parseUserInput("=A2");
    CellBase a4(m_sheet, 1, 4);
    a4.parseUserInput("=A1 + A3");

    QCoreApplication::processEvents(); // handle Damages

    QMap<CellBase, int> depths = m_map->dependencyManager()->depths();
    QCOMPARE(depths[a1], 0);
    QCOMPARE(depths[a2], 1);
    QCOMPARE(depths[a3], 2);
    QCOMPARE(depths[a4], 3);

    a2.parseUserInput("");
    QCoreApplication::processEvents(); // handle Damages

    depths = m_map->dependencyManager()->depths();
    QCOMPARE(depths[a1], 0);
    QCOMPARE(depths[a2], 0);
    QCOMPARE(depths[a3], 1);
    QCOMPARE(depths[a4], 2);
}

void TestDependencies::cleanupTestCase()
{
    delete m_map;
}

QTEST_MAIN(TestDependencies)
