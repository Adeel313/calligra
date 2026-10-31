/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra developers
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef KOINLINEOBJECTHANDLER_H
#define KOINLINEOBJECTHANDLER_H

#include <QObject>
#include <QTextObjectInterface>

// moc only sees declarations in the file it processes, so repeat what
// qabstracttextdocumentlayout.h declares.  This block must be invisible to the
// compiler (which already has the declaration from the include above) and
// visible to moc, which defines Q_MOC_RUN while scanning.
#ifdef Q_MOC_RUN
Q_DECLARE_INTERFACE(QTextObjectInterface, "org.qt-project.Qt.QTextObjectInterface")
#endif

/**
 * Minimal QTextObjectInterface implementation registered on KoTextDocumentLayout.
 *
 * Qt >= 6.8 (commit range around 6.7/6.8) only flags QChar::ObjectReplacementCharacter
 * as an inline object in the text engine when the document layout has at least one
 * registered text object handler (QAbstractTextDocumentLayoutPrivate::hasHandlers()).
 * Calligra handles inline objects itself by overriding
 * resizeInlineObject()/drawInlineObject()/positionInlineObject() on
 * KoTextDocumentLayout and never registered a handler, so after that Qt change
 * inline objects (anchored shapes such as images, page counters, ...) stopped
 * working: the replacement character was laid out as ordinary text and the
 * KoTextDocumentLayout hooks were never called.
 *
 * Registering this handler restores the pre-6.8 behavior.  Qt does not call the
 * methods of this class in the Calligra case, they only exist to satisfy the
 * QTextObjectInterface contract; all real work happens in the
 * KoTextDocumentLayout overrides.
 */
class Q_DECL_HIDDEN KoInlineObjectHandler : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    explicit KoInlineObjectHandler(QObject *parent)
        : QObject(parent)
    {
    }

    QSizeF intrinsicSize(QTextDocument *, int, const QTextFormat &) override
    {
        return QSizeF();
    }

    void drawObject(QPainter *, const QRectF &, QTextDocument *, int, const QTextFormat &) override
    {
    }
};

#endif // KOINLINEOBJECTHANDLER_H
