/* This file is part of the KDE project
 *
 * SPDX-FileCopyrightText: 2013 Inge Wallin <inge@lysator.liu.se>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

// Own
#include "KoOdfParagraphProperties.h"

// Qt
#include <QString>

// odflib
#include <KoXmlStreamReader.h>
#include <KoXmlWriter.h>

#include "Odf2Debug.h"

// ----------------------------------------------------------------
//                         private class

class Q_DECL_HIDDEN KoOdfParagraphProperties::Private
{
public:
    Private();
    ~Private();

    // NYI: Background Image
    KoOdfStyleDropCap *dropCap;
    QList<KoOdfStyleTabStop *> tabStops;
};

KoOdfParagraphProperties::Private::Private()
    : dropCap(nullptr)
{
}

KoOdfParagraphProperties::Private::~Private()
{
    if (dropCap) {
        delete dropCap;
        dropCap = nullptr;
    }
    qDeleteAll(tabStops);
}

// ----------------------------------------------------------------

KoOdfParagraphProperties::KoOdfParagraphProperties()
    : KoOdfStyleProperties()
    , d(new KoOdfParagraphProperties::Private())
{
}

KoOdfParagraphProperties::~KoOdfParagraphProperties()
{
    delete d;
}

void KoOdfParagraphProperties::clear()
{
    KoOdfStyleProperties::clear();

    // FIXME: background image
    if (d->dropCap) {
        delete d->dropCap;
        d->dropCap = nullptr;
    }
    qDeleteAll(d->tabStops);
    d->tabStops.clear();
}

bool KoOdfParagraphProperties::readOdf(KoXmlStreamReader &reader)
{
    bool retval = readAttributes(reader);
    if (!retval) {
        return false;
    }

    // Load child elements.  For paragraph-properties, these are:
    //  - style:background-image
    //  - style:drop-cap
    //  - style:tab-stops
    //
    // Do not use readNextStartElement() here: it stops at *any* end element,
    // which would make us miss all but the first child of list-like elements
    // such as style:tab-stops.
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement()) {
            // End of style:paragraph-properties; all children have been
            // consumed (or skipped) by the code below.
            break;
        }
        if (!reader.isStartElement()) {
            continue;
        }

        const QString child = reader.qualifiedName().toString();
        if (child == "style:background-image") {
            // FIXME: NYI
            reader.skipCurrentElement();
        } else if (child == "style:drop-cap") {
            if (d->dropCap) {
                d->dropCap->attributes.clear();
            } else {
                d->dropCap = new KoOdfStyleDropCap();
            }
            copyAttributes(reader, d->dropCap->attributes);
            reader.skipCurrentElement();
        } else if (child == "style:tab-stops") {
            // Read every style:tab-stop child of this element.
            while (!reader.atEnd()) {
                reader.readNext();
                if (reader.isEndElement() && reader.qualifiedName() == QLatin1StringView("style:tab-stops")) {
                    break;
                }
                if (!reader.isStartElement()) {
                    continue;
                }
                if (reader.qualifiedName() == QLatin1StringView("style:tab-stop")) {
                    KoOdfStyleTabStop *tabStop = new KoOdfStyleTabStop;
                    copyAttributes(reader, tabStop->attributes);
                    d->tabStops.append(tabStop);
                }
                reader.skipCurrentElement();
            }
        } else {
            reader.skipCurrentElement();
        }
    }

    return retval;
}

bool KoOdfParagraphProperties::saveOdf(const QString &propertySet, KoXmlWriter *writer)
{
    Q_UNUSED(propertySet);

    writer->startElement("style:paragraph-properties");
    saveAttributes(writer);

    // Save child elements of style:paragraph-properties
    // FIXME NYI: background image
    if (d->dropCap) {
        writer->startElement("style:drop-cap");
        ::saveAttributes(d->dropCap->attributes, writer);
        writer->endElement(); // style:drop-cap
    }
    if (!d->tabStops.isEmpty()) {
        writer->startElement("style:tab-stops");
        foreach (KoOdfStyleTabStop *tabStop, d->tabStops) {
            writer->startElement("style:tab-stop");
            ::saveAttributes(tabStop->attributes, writer);
            writer->endElement(); // style:tab-stop
        }
        writer->endElement(); // style:tab-stops
    }

    writer->endElement(); // style:paragraph-properties

    return true;
}
