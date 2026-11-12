/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2007 Martin Pfeiffer <hubipete@gmx.net>
                 2009 Jeremias Epperlein <jeeree@web.de>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef FormulaToolEditWidget_H
#define FormulaToolEditWidget_H

#include "ui_FormulaToolEditWidget.h"

class QComboBox;

class KoFormulaShape;
class KoFormulaTool;

/**
 * @short A widget providing options for the FormulaTool
 *
 * The FormulaToolEditWidget is the source entry of the formula tool: the typed
 * LaTeX (or, when the Cauchy library is available, Matlab/Octave) expression is
 * converted to MathML and replaces the content of the formula being edited. It
 * lives on the same ribbon tab as the element and symbol buttons of the tool.
 */
class FormulaToolEditWidget : public QWidget, Ui::FormulaToolEditWidget
{
    Q_OBJECT
public:
    /// Standard constructor
    explicit FormulaToolEditWidget(KoFormulaTool *tool, QWidget *parent = nullptr);

    /// Standard destructor
    ~FormulaToolEditWidget() override;

    /// Set the KoFormulaTool @p tool this options widget belongs to
    void setFormulaTool(KoFormulaTool *tool);

public Q_SLOTS:
    /// Show the source the formula @p shape was last built from
    void setFormulaShape(KoFormulaShape *shape);

    /// Convert the typed source to MathML and apply it to the formula
    void applySource();

private:
    /// @return the source converted to MathML, or an empty string on failure
    QString mathMLFromSource(const QString &source, QString *error) const;
    /// @return the markup language the source is written in
    QString sourceMode() const;
    void showError(const QString &error);

    /// The KoFormulaTool this options widget belongs to
    KoFormulaTool *m_tool;
    QComboBox *m_modeCombo;
};

#endif // FormulaToolEditWidget_H
