/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2007 Martin Pfeiffer <hubipete@gmx.net>
                 2009 Jeremias Epperlein <jeeree@web.de>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "FormulaToolEditWidget.h"

#include "3rdparty/itexToMML/itex2MML.h"
#include "AnnotationElement.h"
#include "BasicElement.h"
#include "FormulaCommand.h"
#include "FormulaCommandUpdate.h"
#include "FormulaElement.h"
#include "KoFormulaShape.h"
#include "KoFormulaTool.h"

#ifdef HAVE_M2MML
#include <m2mml.h>
#endif

#include <KoCanvasBase.h>
#include <KoXmlReader.h>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>

namespace
{
QString toQString(const std::string &text)
{
    return QString::fromLatin1(text.data(), int(text.size()));
}

std::string toStdString(const QString &text)
{
    const QByteArray latin1 = text.toLatin1();
    return std::string(latin1.constData(), latin1.length());
}
}

FormulaToolEditWidget::FormulaToolEditWidget(KoFormulaTool *tool, QWidget *parent)
    : QWidget(parent)
    , m_tool(tool)
    , m_modeCombo(nullptr)
{
    setupUi(this);
    setWindowTitle(i18n("LaTeX"));
    errorLabel->hide();

#ifdef HAVE_M2MML
    // The Cauchy library also understands Matlab/Octave notation.
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(i18n("LaTeX"));
    m_modeCombo->addItem(i18n("Matlab"));
    latexLayout->insertWidget(0, new QLabel(i18n("Mode:"), this));
    latexLayout->insertWidget(1, m_modeCombo);
#endif

    connect(latexEdit, &QLineEdit::editingFinished, this, &FormulaToolEditWidget::applySource);
    connect(m_tool, &KoFormulaTool::formulaShapeChanged, this, &FormulaToolEditWidget::setFormulaShape);
    if (m_tool->shape()) {
        setFormulaShape(m_tool->shape());
    }
}

FormulaToolEditWidget::~FormulaToolEditWidget() = default;

void FormulaToolEditWidget::setFormulaTool(KoFormulaTool *tool)
{
    m_tool = tool;
}

void FormulaToolEditWidget::setFormulaShape(KoFormulaShape *shape)
{
    if (!shape || !shape->formulaData()) {
        return;
    }

    QString source;
    QString mode;
    const QList<BasicElement *> children = shape->formulaData()->formulaElement()->childElements();
    for (BasicElement *element : children) {
        if (element->elementType() != Annotation) {
            continue;
        }
        auto *annotation = static_cast<AnnotationElement *>(element);
        source = annotation->content();
        mode = annotation->attribute("mode");
    }

    latexEdit->setText(source);
#ifdef HAVE_M2MML
    if (m_modeCombo) {
        m_modeCombo->setCurrentIndex(mode == QLatin1String("Matlab") ? 1 : 0);
    }
#else
    Q_UNUSED(mode)
#endif
}

void FormulaToolEditWidget::applySource()
{
    KoFormulaShape *shape = m_tool ? m_tool->shape() : nullptr;
    if (!shape) {
        showError(i18n("Select a formula to edit it as source."));
        return;
    }

    const QString source = latexEdit->text().trimmed();
    if (source.isEmpty()) {
        showError(QString());
        return;
    }

    QString error;
    const QString mathml = mathMLFromSource(source, &error);
    if (mathml.isEmpty()) {
        showError(error);
        return;
    }

    KoXmlDocument document;
    if (!document.setContent(mathml, false, nullptr, nullptr, nullptr)) {
        showError(i18n("Parse error."));
        return;
    }

    auto formulaElement = std::make_unique<FormulaElement>();
    formulaElement->readMathML(document.documentElement());
    auto *annotation = new AnnotationElement;
    annotation->setContent(source);
    annotation->setAttribute("mode", sourceMode());
    formulaElement->insertChild(0, annotation);

    m_tool->canvas()->addCommand(new FormulaCommandUpdate(shape, new FormulaCommandLoad(shape->formulaData(), std::move(formulaElement))));
    showError(QString());
}

QString FormulaToolEditWidget::mathMLFromSource(const QString &source, QString *error) const
{
    const std::string text = toStdString(source);

#ifdef HAVE_M2MML
    if (sourceMode() == QLatin1String("Matlab")) {
        std::string mathml;
        std::string message;
        if (m2mml(text, mathml, &message)) {
            return toQString(mathml);
        }
        *error = toQString(message);
        return QString();
    }
#endif

    const std::string latex = '$' + text + '$';
    char *mathml = itex2MML_parse(latex.c_str(), int(latex.size()));
    if (!mathml) {
        *error = i18n("Parse error.");
        return QString();
    }
    const QString result = toQString(mathml);
    itex2MML_free_string(mathml);
    return result;
}

QString FormulaToolEditWidget::sourceMode() const
{
#ifdef HAVE_M2MML
    if (m_modeCombo && m_modeCombo->currentIndex() == 1) {
        return QStringLiteral("Matlab");
    }
#endif
    return QStringLiteral("LaTeX");
}

void FormulaToolEditWidget::showError(const QString &error)
{
    errorLabel->setText(error);
    errorLabel->setVisible(!error.isEmpty());
}
