/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2013 Inge Wallin <inge@lysator.liu.se>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef OPCRELSET_H
#define OPCRELSET_H

#include <QList>
#include <QString>

#include <KoFilter.h>

class KoStore;

/**
 * @brief this class manages a set of relationships in a file in the OPC format
 *
 * OPC is an abbreviation for the Open Package Conventions and is a
 * format that is the basis for several other file formats.  Examples
 * include MS OOXML file formats docx, xlsx and pptx but also many
 * others.
 *
 * This class handles the contents of one .rels file, i.e. the
 * relations for one file inside the OPC store.  Lines in that .rels
 * file looks like the following:
 *
 *   <Relationship
 *     Id="rId1"
 *     Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles"
 *     Target="styles.xml"
 *   />
 */
class OpcRelSet
{
public:
    struct Relationship {
        QString id;
        QString type;
        QString target;
        bool external = false;
    };

    OpcRelSet();
    ~OpcRelSet();

    // FIXME: So far we only design this for writing.  In the general
    //        case we should also lookup and reading.

    // Return the ID
    QString addRelationship(const QString &type, const QString &target, bool external = false);
    QList<Relationship> relationships() const;
    KoFilter::ConversionStatus writeToStore(KoStore *store, const QString &path) const;

private:
    // Privat functions

private:
    QList<Relationship> m_relationships;
};

#endif // OPCRELSET_H
