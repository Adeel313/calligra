/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef TESTHEADERFOOTERLAYOUT_H
#define TESTHEADERFOOTERLAYOUT_H

#include <QObject>

class TestHeaderFooterLayout : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testStableHeight_data();
    void testStableHeight();
};

#endif
