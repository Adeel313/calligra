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
    void testImageDrag_data();
    void testImageDrag();
    void testFloatingImageBounds_data();
    void testFloatingImageBounds();
    void testTypingBesideNewImage_data();
    void testTypingBesideNewImage();
    void testRemoveHeaderFooterPolicy_data();
    void testRemoveHeaderFooterPolicy();
};

#endif
