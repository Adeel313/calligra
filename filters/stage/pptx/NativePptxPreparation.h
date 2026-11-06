/* SPDX-License-Identifier: LGPL-2.0-or-later */
// Saved DrawingML normalization shared with the reference worker; no office
// process is used by the native filter. The caller owns the disposable copy.
#include <KZip>
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <QDomDocument>
#include <QDir>
#include <QFileInfo>
#include <QXmlStreamReader>
namespace {
bool rewritePackage(const QString &file, const QMap<QString, QByteArray> &updates)
{
    KZip input(file);
    if (!input.open(QIODevice::ReadOnly)) return false;
    const QString replacement = file + QStringLiteral(".normalized");
    KZip output(replacement);
    if (!output.open(QIODevice::WriteOnly)) return false;
    const auto copy = [&](const auto &self, const KArchiveDirectory *directory, const QString &prefix) -> bool {
        for (const QString &name : directory->entries()) {
            const auto *entry = directory->entry(name);
            const QString path = prefix + name;
            if (entry->isDirectory()) {
                if (!self(self, static_cast<const KArchiveDirectory *>(entry), path + QLatin1Char('/'))) return false;
            } else if (entry->isFile()) {
                const QByteArray bytes = updates.contains(path) ? updates.value(path) : static_cast<const KArchiveFile *>(entry)->data();
                if (!output.writeFile(path, bytes)) return false;
            }
        }
        return true;
    };
    if (!copy(copy, input.directory(), {}) || !output.close()) return false;
    input.close();
    return QFile::remove(file) && QFile::rename(replacement, file);
}

#include "../../libmsooxml/pptx/NativePptxDiagrams.h"
}
