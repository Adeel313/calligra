/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KOOOXMLDEFAULTS_P_H
#define KOOOXMLDEFAULTS_P_H

#include <QByteArray>

inline QByteArray koDefaultSaveMimeType(const QByteArray &nativeMimeType)
{
    if (nativeMimeType == "application/vnd.oasis.opendocument.text")
        return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (nativeMimeType == "application/vnd.oasis.opendocument.presentation")
        return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    if (nativeMimeType == "application/vnd.oasis.opendocument.spreadsheet")
        return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    return nativeMimeType;
}

#endif
