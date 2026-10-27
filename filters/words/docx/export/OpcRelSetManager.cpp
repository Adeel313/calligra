/* This file is part of the KDE project
 *
 * SPDX-FileCopyrightText: 2013-2014 Inge Wallin <inge@lysator.liu.se>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

// Own
#include "OpcRelSetManager.h"

// Qt
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QString>

// odf lib
#include "KoStore.h"
#include <KoXmlNS.h>
#include <KoXmlStreamReader.h>

// Filter libraries
#include "KoOdfStyle.h"

// This filter
#include "DocxExportDebug.h"
#include "OpcRelSet.h"

// ================================================================
//                 class OpcRelSetManager

class OpcRelSetManager::Private
{
public:
    Private();
    ~Private();

    QHash<QString, OpcRelSet *> relSets; // path, relations
    OpcRelSet *documentRelSet; // The relations for the whole document
                               // Stored in _rels/.rels
};

static QString normalizedPartPath(const QString &path)
{
    QString normalized = QDir::cleanPath(path);
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    return normalized;
}

OpcRelSetManager::Private::Private()
    : documentRelSet(nullptr)
{
}

OpcRelSetManager::Private::~Private()
{
    qDeleteAll(relSets);
    delete documentRelSet;
}

// ----------------------------------------------------------------

OpcRelSetManager::OpcRelSetManager()
    : d(new OpcRelSetManager::Private())
{
}

OpcRelSetManager::~OpcRelSetManager()
{
    delete d;
}

OpcRelSet *OpcRelSetManager::relSet(const QString &path) const
{
    return d->relSets.value(normalizedPartPath(path), nullptr);
}

void OpcRelSetManager::setRelSet(const QString &path, OpcRelSet *relSet)
{
    const QString normalizedPath = normalizedPartPath(path);
    OpcRelSet *oldRelSet = d->relSets.value(normalizedPath, nullptr);
    if (oldRelSet != relSet) {
        delete oldRelSet;
        d->relSets.insert(normalizedPath, relSet);
    }
}

OpcRelSet *OpcRelSetManager::documentRelSet() const
{
    return d->documentRelSet;
}

void OpcRelSetManager::setDocumentRelSet(OpcRelSet *relSet)
{
    if (d->documentRelSet != relSet) {
        delete d->documentRelSet;
    }
    d->documentRelSet = relSet;
}

void OpcRelSetManager::clear()
{
    qDeleteAll(d->relSets);
    d->relSets.clear();

    if (d->documentRelSet) {
        delete d->documentRelSet;
        d->documentRelSet = nullptr;
    }
}

// ----------------------------------------------------------------

bool OpcRelSetManager::loadRelSets(KoStore *odfStore)
{
    Q_UNUSED(odfStore);

    // QString errorMsg;
    // int errorLine;
    // int errorColumn;

    KoXmlStreamReader reader;
    // FIXME: Add expected namespaces for rels here.

    // FIXME: Look up all .rels file and load them here.

    // FIXME: Return actual return status.
    return true;
}

bool OpcRelSetManager::saveRelSets(KoStore *odfStore)
{
    if (!odfStore) {
        return false;
    }

    if (d->documentRelSet && d->documentRelSet->writeToStore(odfStore, QStringLiteral("_rels/.rels")) != KoFilter::OK) {
        return false;
    }

    for (auto it = d->relSets.cbegin(); it != d->relSets.cend(); ++it) {
        const QFileInfo sourcePart(it.key());
        const QString directory = sourcePart.path() == QLatin1String(".") ? QString() : sourcePart.path() + QLatin1Char('/');
        const QString relsPath = directory + QStringLiteral("_rels/") + sourcePart.fileName() + QStringLiteral(".rels");
        if (!it.value() || it.value()->writeToStore(odfStore, relsPath) != KoFilter::OK) {
            return false;
        }
    }
    return true;
}
