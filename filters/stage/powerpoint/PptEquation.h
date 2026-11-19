// SPDX-License-Identifier: LGPL-2.0-or-later
#pragma once

#include <QByteArray>

namespace PptEquation
{
// Returns a MathML element for a supported, nonempty Equation Native object.
QByteArray mathML(const QByteArray &embeddedObject);
}
