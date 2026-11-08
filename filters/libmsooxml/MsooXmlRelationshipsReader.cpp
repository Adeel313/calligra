/*
 * This file is part of Office 2007 Filters for Calligra
 *
 * SPDX-FileCopyrightText: 2009 Nokia Corporation and /or its subsidiary(-ies).
 *
 * Contact: Suresh Chande suresh.chande@nokia.com
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include "MsooXmlRelationshipsReader.h"
#include <MsooXmlSchemas.h>
#include <MsooXmlUtils.h>

#include <QDir>
#include <QUrl>

// #define MSOOXML_CURRENT_NS
#define MSOOXML_CURRENT_CLASS MsooXmlRelationshipsReader
#define BIND_READ_CLASS MSOOXML_CURRENT_CLASS

#include <MsooXmlReader_p.h>

using namespace MSOOXML;

MsooXmlRelationshipsReaderContext::MsooXmlRelationshipsReaderContext(const QString &_path,
                                                                     const QString &_file,
                                                                     QMap<QString, QString> &_rels,
                                                                     QMap<QString, QString> &_targetsForTypes)
    : path(_path)
    , file(_file)
    , rels(&_rels)
    , targetsForTypes(&_targetsForTypes)
{
}

class MsooXmlRelationshipsReader::Private
{
public:
    Private() = default;
    ~Private() = default;
    QString pathAndFile;
};

MsooXmlRelationshipsReader::MsooXmlRelationshipsReader(KoOdfWriters *writers)
    : MSOOXML::MsooXmlReader(writers)
    , m_context(nullptr)
    , d(new Private)
{
    init();
}

MsooXmlRelationshipsReader::~MsooXmlRelationshipsReader()
{
    delete d;
}

void MsooXmlRelationshipsReader::init()
{
}

KoFilter::ConversionStatus MsooXmlRelationshipsReader::read(MSOOXML::MsooXmlReaderContext *context)
{
    m_context = dynamic_cast<MsooXmlRelationshipsReaderContext *>(context);
    const KoFilter::ConversionStatus result = readInternal();
    m_context = nullptr;
    if (result == KoFilter::OK)
        return KoFilter::OK;
    return result;
}

KoFilter::ConversionStatus MsooXmlRelationshipsReader::readInternal()
{
    debugMsooXml << "=============================";

    d->pathAndFile = MsooXmlRelationshipsReader::relKey(m_context->path, m_context->file, QString());

    readNext();
    if (!isStartDocument()) {
        return KoFilter::WrongFormat;
    }

    // Relationships
    readNext();
    debugMsooXml << *this << namespaceUri();

    if (!expectEl("Relationships")) {
        return KoFilter::WrongFormat;
    }
    if (!expectNS(MSOOXML::Schemas::relationships)) {
        return KoFilter::WrongFormat;
    }
    /*
        const QXmlStreamAttributes attrs( attributes() );
        for (int i=0; i<attrs.count(); i++) {
            debugMsooXml << "1 NS prefix:" << attrs[i].name() << "uri:" << attrs[i].namespaceUri();
        }*/

    QXmlStreamNamespaceDeclarations namespaces(namespaceDeclarations());
    for (int i = 0; i < namespaces.count(); i++) {
        debugMsooXml << "NS prefix:" << namespaces[i].prefix() << "uri:" << namespaces[i].namespaceUri();
    }

    TRY_READ(Relationships)
    debugMsooXml << "===========finished============";
    return KoFilter::OK;
}

#undef CURRENT_EL
#define CURRENT_EL Relationships
//! Relationships handler
/*!
 No parent elements.

 Child elements:
    - [done] Relationship
*/
KoFilter::ConversionStatus MsooXmlRelationshipsReader::read_Relationships()
{
    READ_PROLOGUE
    while (!atEnd()) {
        readNext();
        debugMsooXml << *this;
        BREAK_IF_END_OF(CURRENT_EL)
        if (isStartElement()) {
            TRY_READ_IF(Relationship)
            ELSE_WRONG_FORMAT
        }
    }
    READ_EPILOGUE
}

#undef CURRENT_EL
#define CURRENT_EL Relationship
//! Relationship handler
/*!
 Parent elements:
    - [done] Relationships

 No child elements.
*/
KoFilter::ConversionStatus MsooXmlRelationshipsReader::read_Relationship()
{
    READ_PROLOGUE
    const QXmlStreamAttributes attrs(attributes());
    READ_ATTR_WITHOUT_NS(Id)
    READ_ATTR_WITHOUT_NS(Type)
    READ_ATTR_WITHOUT_NS(Target)
    QString targetPath;
    if (attrs.value(QLatin1String("TargetMode")) == QLatin1String("External")) {
        targetPath = Target;
    } else {
        // OPC relationship targets are URI references. Decode escaped path
        // characters before looking the target up in the ZIP package; ZIP
        // member names contain the decoded characters (for example a space,
        // not "%20"). Keep external URLs verbatim above.
        const QString decodedTarget = QUrl::fromPercentEncoding(Target.toUtf8());
        if (decodedTarget.startsWith(QLatin1Char('/'))) {
        // OPC relationship targets beginning with '/' are rooted at the
        // package, rather than relative to the source part's directory.
            targetPath = QDir::cleanPath(decodedTarget.mid(1));
        } else {
            targetPath = QDir::cleanPath(m_context->path + QLatin1Char('/') + decodedTarget);
        }
    }
    // debugMsooXml << "adding rel:";
    // debugMsooXml << d->pathAndFile + Id;
    // debugMsooXml << fixedPath + '/' + Target;

    m_context->rels->insert(d->pathAndFile + Id, targetPath);
    // debugMsooXml << "added target" << Target << "for type" << Type << "path=" << fixedPath << "key=" << targetKey(m_context->path + '/' + m_context->file,
    // Type);
    // Strict documents use purl.oclc.org relationship types; store them under the
    // transitional type so that targetForType() lookups work for both dialects.
    m_context->targetsForTypes->insert(
        targetKey(m_context->path + '/' + m_context->file, MSOOXML::Utils::canonicalNamespace(Type)), targetPath);

    readNext();
    READ_EPILOGUE
}
