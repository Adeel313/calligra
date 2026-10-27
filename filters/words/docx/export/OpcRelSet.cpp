/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2012 Inge Wallin <inge@lysator.liu.se>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Own
#include "OpcRelSet.h"

// Qt
#include <QByteArray>
#include <QList>
#include <QString>

// This filter
#include "DocxExportDebug.h"
#include <KoStore.h>
#include <KoStoreDevice.h>
#include <KoXmlWriter.h>

// ================================================================
//                         class OpcRelSet

OpcRelSet::OpcRelSet() = default;

OpcRelSet::~OpcRelSet() = default;

QString OpcRelSet::addRelationship(const QString &type, const QString &target, bool external)
{
    if (type.isEmpty() || target.isEmpty()) {
        return QString();
    }

    for (const Relationship &relationship : std::as_const(m_relationships)) {
        if (relationship.type == type && relationship.target == target && relationship.external == external) {
            return relationship.id;
        }
    }

    Relationship relationship;
    relationship.id = QStringLiteral("rId%1").arg(m_relationships.size() + 1);
    relationship.type = type;
    relationship.target = target;
    relationship.external = external;
    m_relationships.append(relationship);
    return relationship.id;
}

QList<OpcRelSet::Relationship> OpcRelSet::relationships() const
{
    return m_relationships;
}

KoFilter::ConversionStatus OpcRelSet::writeToStore(KoStore *store, const QString &path) const
{
    if (!store || !store->open(path)) {
        warnDocx << "Unable to create relationship part:" << path;
        return KoFilter::CreationError;
    }

    KoStoreDevice device(store);
    KoXmlWriter writer(&device);
    writer.startDocument(nullptr, nullptr, nullptr);
    writer.startElement("Relationships");
    writer.addAttribute("xmlns", "http://schemas.openxmlformats.org/package/2006/relationships");
    for (const Relationship &relationship : std::as_const(m_relationships)) {
        writer.startElement("Relationship");
        writer.addAttribute("Id", relationship.id);
        writer.addAttribute("Type", relationship.type);
        writer.addAttribute("Target", relationship.target);
        if (relationship.external) {
            writer.addAttribute("TargetMode", "External");
        }
        writer.endElement();
    }
    writer.endElement();
    writer.endDocument();

    return store->close() ? KoFilter::OK : KoFilter::CreationError;
}
