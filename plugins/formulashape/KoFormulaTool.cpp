/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2006 Martin Pfeiffer <hubipete@gmx.net>
                 2009 Jeremias Epperlein <jeeree@web.de>

   SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KoFormulaTool.h"

#include "BasicElement.h"
#include "ElementFactory.h"
#include "FormulaDebug.h"
#include "FormulaEditor.h"
#include "FormulaElement.h"
#include "FormulaLinearInput.h"
#include "FormulaToolEditWidget.h"
#include "FormulaRibbonWidgets.h"
#include "KoFormulaShape.h"
#include "RowElement.h"
#include "TokenElement.h"

#include <KoCanvasBase.h>
#include <KoIcon.h>
#include <KoPointerEvent.h>
#include <KoSelection.h>
#include <KoShapeController.h>

#include <KLocalizedString>

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>

#include "FormulaCommand.h"
#include "FormulaCommandUpdate.h"
#include "FormulaRenderer.h"
#include <KoEmbeddedDocumentSaver.h>
#include <KoGenStyles.h>
#include <KoOdfLoadingContext.h>
#include <KoOdfStylesReader.h>
#include <KoShapeLoadingContext.h>
#include <KoShapeSavingContext.h>
#include <KoXmlReader.h>
#include <KoXmlWriter.h>
#include <QClipboard>

#include <memory>

KoFormulaTool::KoFormulaTool(KoCanvasBase *canvas)
    : KoToolBase(canvas)
    , m_formulaShape(nullptr)
    , m_formulaEditor(nullptr)
{
    setupActions();
    setTextMode(true);
}

KoFormulaTool::~KoFormulaTool()
{
    if (m_formulaEditor) {
        m_cursorList.removeAll(m_formulaEditor);
        delete m_formulaEditor;
    }
    foreach (FormulaEditor *tmp, m_cursorList) {
        delete tmp;
    }
}

void KoFormulaTool::activate(ToolActivation toolActivation, const QSet<KoShape *> &shapes)
{
    Q_UNUSED(toolActivation);
    for (KoShape *shape : shapes) {
        m_formulaShape = dynamic_cast<KoFormulaShape *>(shape);
        if (m_formulaShape) {
            break;
        }
    }

    if (m_formulaShape == nullptr) { // none found
        Q_EMIT done();
        return;
    }
    useCursor(Qt::IBeamCursor);
    m_formulaEditor = nullptr;
    for (int i = 0; i < m_cursorList.count(); i++) {
        FormulaEditor *editor = m_cursorList[i];
        FormulaData *formulaData = m_formulaShape->formulaData();
        if (editor->formulaData() == formulaData) {
            // we have to check if the cursors current element is actually a
            // child of the m_formulaShape->formulaData()
            m_cursorList.removeAll(editor);
            if (formulaData->formulaElement()->hasDescendant(editor->cursor().currentElement())) {
                if (editor->cursor().isAccepted()) {
                    debugFormula << "Found old cursor";
                    m_formulaEditor = editor;
                    break;
                }
            }
            delete editor;
        }
    }
    if (m_formulaEditor == nullptr) {
        // TODO: there should be a extra constructor for this
        m_formulaEditor = new FormulaEditor(m_formulaShape->formulaData());
    }
    connect(m_formulaShape->formulaData(), &FormulaData::dataChanged, this, &KoFormulaTool::updateCursor);
    for (const TemplateAction &templateAction : std::as_const(m_templateActions)) {
        connect(templateAction.action, &QAction::triggered, this, [this, templateAction] {
            insert(templateAction.data);
        });
    }
    // Only for debugging:
    connect(action("write_elementTree"), &QAction::triggered, m_formulaShape->formulaData(), &FormulaData::writeElementTree);

    // Show the formula enlarged while it is being edited.
    m_formulaShape->setEditing(true);

    Q_EMIT formulaShapeChanged(m_formulaShape);
}

void KoFormulaTool::deactivate()
{
    for (const TemplateAction &templateAction : std::as_const(m_templateActions)) {
        disconnect(templateAction.action, &QAction::triggered, this, nullptr);
    }
    if (!m_formulaShape) {
        // activated without shape
        return;
    }
    disconnect(m_formulaShape->formulaData(), nullptr, this, nullptr);
    if (canvas()) {
        m_cursorList.append(m_formulaEditor);
        debugFormula << "Appending cursor";
    }
    if (m_cursorList.count() > 20) { // don't let it grow indefinitely
        delete m_cursorList.takeAt(0);
    }
    // Editing is over: show the formula at the size of the surrounding text again.
    m_formulaShape->setEditing(false);
    m_formulaShape = nullptr;
}

void KoFormulaTool::updateCursor(FormulaCommand *command, bool undo)
{
    if (command != nullptr) {
        debugFormula << "Going to change cursor";
        command->changeCursor(m_formulaEditor->cursor(), undo);
    } else {
        debugFormula << "Going to reset cursor";
        resetFormulaEditor();
    }
    repaintCursor();
}

void KoFormulaTool::paint(QPainter &painter, const KoViewConverter &converter)
{
    painter.save();
    // transform painter from view coordinate system to document coordinate system
    // remember that matrix multiplication is not commutative so painter.matrix
    // has to come last
    painter.setTransform(m_formulaShape->absoluteTransformation(&converter) * painter.transform());
    KoShape::applyConversion(painter, converter);
    // While the formula is edited it is shown enlarged; the cursor positions are
    // in the (unscaled) element coordinates, so the same scale applies to them.
    const qreal scale = m_formulaShape->editingScale();
    if (scale != 1.0) {
        painter.scale(scale, scale);
    }
    m_formulaShape->formulaRenderer()->paintElement(painter, m_formulaShape->formulaData()->formulaElement(), true);
    m_formulaEditor->paint(painter);
    painter.restore();
}

void KoFormulaTool::repaintCursor()
{
    canvas()->updateCanvas(m_formulaShape->boundingRect());
}

void KoFormulaTool::mousePressEvent(KoPointerEvent *event)
{
    // Check if the event is valid means inside the shape
    if (!m_formulaShape->boundingRect().contains(event->point)) {
        return;
    }
    // transform the global coordinates into shape coordinates
    QPointF p = m_formulaShape->absoluteTransformation(nullptr).inverted().map(event->point);
    // the elements are laid out unscaled, the shape is only drawn magnified
    p /= m_formulaShape->editingScale();
    if (event->modifiers() & Qt::ShiftModifier) {
        m_formulaEditor->cursor().setSelecting(true);
    } else {
        m_formulaEditor->cursor().setSelecting(false);
    }
    // set the cursor to the element the user clicked on
    m_formulaEditor->cursor().setCursorTo(p);

    repaintCursor();
    event->accept();
}

void KoFormulaTool::mouseDoubleClickEvent(KoPointerEvent *event)
{
    if (!m_formulaShape->boundingRect().contains(event->point)) {
        return;
    }
    // transform the global coordinates into shape coordinates
    QPointF p = m_formulaShape->absoluteTransformation(nullptr).inverted().map(event->point);
    // the elements are laid out unscaled, the shape is only drawn magnified
    p /= m_formulaShape->editingScale();

    // clear the current selection
    m_formulaEditor->cursor().setSelecting(false);
    // place the cursor
    m_formulaEditor->cursor().setCursorTo(p);
    m_formulaEditor->cursor().selectElement(m_formulaEditor->cursor().currentElement());
    repaintCursor();
    event->accept();
}

void KoFormulaTool::mouseMoveEvent(KoPointerEvent *event)
{
    //     Q_UNUSED( event )
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }
    // Check if the event is valid means inside the shape
    if (!m_formulaShape->boundingRect().contains(event->point))
        debugFormula << "Getting most probably invalid mouseMoveEvent";

    // transform the global coordinates into shape coordinates
    QPointF p = m_formulaShape->absoluteTransformation(nullptr).inverted().map(event->point);
    // the elements are laid out unscaled, the shape is only drawn magnified
    p /= m_formulaShape->editingScale();
    // TODO Implement drag and drop of elements
    m_formulaEditor->cursor().setSelecting(true);
    m_formulaEditor->cursor().setCursorTo(p);
    repaintCursor();
    event->accept();
}

void KoFormulaTool::mouseReleaseEvent(KoPointerEvent *event)
{
    Q_UNUSED(event)

    // TODO Implement drag and drop
}

void KoFormulaTool::keyPressEvent(QKeyEvent *event)
{
    FormulaCommand *command = nullptr;
    if (!m_formulaEditor)
        return;

    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down
        || event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
        if (event->modifiers() & Qt::ShiftModifier) {
            m_formulaEditor->cursor().setSelecting(true);
        } else {
            m_formulaEditor->cursor().setSelecting(false);
        }
    }
    // An equation editor builds "a/b" or "x^2" up into the fraction or the
    // superscript as soon as a space ends the expression. Do the same here
    // instead of inserting a plain space.
    if (event->key() == Qt::Key_Space && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
        && buildUpLinearInput()) {
        event->accept();
        return;
    }

    switch (event->key()) // map key to movement or action
    {
    case Qt::Key_Backspace:
        m_formulaShape->update();
        command = m_formulaEditor->remove(true);
        m_formulaShape->updateLayout();
        m_formulaShape->update();
        break;
    case Qt::Key_Delete:
        m_formulaShape->update();
        command = m_formulaEditor->remove(false);
        m_formulaShape->updateLayout();
        m_formulaShape->update();
        break;
    case Qt::Key_Left:
        m_formulaEditor->cursor().move(MoveLeft);
        break;
    case Qt::Key_Up:
        m_formulaEditor->cursor().move(MoveUp);
        break;
    case Qt::Key_Right:
        m_formulaEditor->cursor().move(MoveRight);
        break;
    case Qt::Key_Down:
        m_formulaEditor->cursor().move(MoveDown);
        break;
    case Qt::Key_End:
        m_formulaEditor->cursor().moveEnd();
        break;
    case Qt::Key_Home:
        m_formulaEditor->cursor().moveHome();
        break;
    default:
        if (event->text().length() != 0) {
            command = m_formulaEditor->insertText(event->text());
        }
    }
    if (command != nullptr) {
        canvas()->addCommand(new FormulaCommandUpdate(m_formulaShape, command));
    }
    repaintCursor();
    event->accept();
}

void KoFormulaTool::keyReleaseEvent(QKeyEvent *event)
{
    event->accept();
}

void KoFormulaTool::remove(bool backSpace)
{
    m_formulaShape->update();
    m_formulaEditor->remove(backSpace);
    m_formulaShape->updateLayout();
    m_formulaShape->update();
}

void KoFormulaTool::insert(const QString &action)
{
    FormulaCommand *command;
    m_formulaShape->update();
    command = m_formulaEditor->insertMathML(action);
    if (command != nullptr) {
        canvas()->addCommand(new FormulaCommandUpdate(m_formulaShape, command));
    }
}

void KoFormulaTool::changeTable(QAction *action)
{
    FormulaCommand *command;
    m_formulaShape->update();
    bool row = action->data().toList()[0].toBool();
    bool insert = action->data().toList()[1].toBool();
    command = m_formulaEditor->changeTable(insert, row);
    if (command != nullptr) {
        canvas()->addCommand(new FormulaCommandUpdate(m_formulaShape, command));
    }
}

void KoFormulaTool::insertSymbol(const QString &symbol)
{
    FormulaCommand *command;
    m_formulaShape->update();
    command = m_formulaEditor->insertText(symbol);
    if (command != nullptr) {
        canvas()->addCommand(new FormulaCommandUpdate(m_formulaShape, command));
    }
}

bool KoFormulaTool::buildUpLinearInput()
{
    if (!m_formulaShape || !m_formulaEditor) {
        return false;
    }
    FormulaCursor &cursor = m_formulaEditor->cursor();
    if (cursor.hasSelection() || !cursor.insideToken()) {
        return false;
    }
    auto *token = dynamic_cast<TokenElement *>(cursor.currentElement());
    if (!token) {
        return false;
    }

    const QString text = token->text();
    const int caret = qBound(0, cursor.position(), static_cast<int>(text.length()));
    const QString typed = text.left(caret);
    // Only what follows the last space is an expression; everything before it
    // is ordinary text and stays where it is.
    const int spacePosition = typed.lastIndexOf(QLatin1Char(' '));
    const QString prefix = typed.left(spacePosition + 1);
    const QString expression = typed.mid(spacePosition + 1);
    if (!FormulaLinearInput::hasLinearSyntax(expression)) {
        return false;
    }
    const QString mathML = FormulaLinearInput::toMathML(expression);
    if (mathML.isEmpty()) {
        return false;
    }

    KoXmlDocument document;
    if (!document.setContent(mathML, false, nullptr, nullptr, nullptr)) {
        return false;
    }
    auto formula = std::make_unique<FormulaElement>();
    if (!formula->readMathML(document.documentElement())) {
        return false;
    }
    QList<BasicElement *> builtElements = formula->childElements();
    if (builtElements.isEmpty()) {
        return false;
    }
    // The elements are handed over to the command, which owns them from here on.
    for (BasicElement *element : builtElements) {
        formula->removeChild(element);
    }

    const auto makeToken = [](const QString &content) {
        auto element = ElementFactory::createElement(content.isEmpty() || content.at(0).isDigit() ? QStringLiteral("mn") : QStringLiteral("mi"),
                                                     nullptr);
        auto *tokenElement = static_cast<TokenElement *>(element.get());
        tokenElement->insertText(0, content);
        element.release();
        return tokenElement;
    };

    QList<BasicElement *> replacements;
    if (!prefix.isEmpty()) {
        replacements.append(makeToken(prefix));
    }
    replacements.append(builtElements);
    const QString rest = text.mid(caret);
    if (!rest.isEmpty()) {
        replacements.append(makeToken(rest));
    }

    auto *row = dynamic_cast<RowElement *>(token->parentElement());
    const int position = row ? row->childElements().indexOf(token) : -1;
    if (position < 0) {
        qDeleteAll(replacements);
        return false;
    }

    auto *command = new FormulaCommandReplaceElements(row, position, 1, replacements, false);
    command->setText(kundo2_i18n("Build up formula"));
    canvas()->addCommand(new FormulaCommandUpdate(m_formulaShape, command));
    m_formulaShape->updateLayout();
    m_formulaShape->update();
    repaintCursor();
    return true;
}

QList<QPointer<QWidget>> KoFormulaTool::createOptionWidgets()
{
    auto structuresOption = new FormulaStructuresWidget(this);
    auto symbolsOption = new FormulaSymbolsWidget(this);

    auto editOption = new FormulaToolEditWidget(this);
    editOption->setFormulaTool(this);
    structuresOption->setProperty("ribbonGroupOrder", 20);
    symbolsOption->setProperty("ribbonGroupOrder", 10);
    editOption->setProperty("ribbonGroupOrder", 30);
    return {symbolsOption, structuresOption, editOption};
}

KoFormulaShape *KoFormulaTool::shape()
{
    return m_formulaShape;
}

FormulaEditor *KoFormulaTool::formulaEditor()
{
    return m_formulaEditor;
}

void KoFormulaTool::resetFormulaEditor()
{
    m_formulaEditor->setData(m_formulaShape->formulaData());
    FormulaCursor cursor(FormulaCursor(m_formulaShape->formulaData()->formulaElement(), false, 0, 0));
    m_formulaEditor->setCursor(cursor);
    // if the cursor is not allowed at the beginning of the formula, move it right
    // TODO: check, if this can ever happen
    if (!m_formulaEditor->cursor().isAccepted()) {
        m_formulaEditor->cursor().move(MoveRight);
    }
}

void KoFormulaTool::loadFormula()
{
    // get an filepath
    const QString fileName = QFileDialog::getOpenFileName();
    if (fileName.isEmpty() || !shape())
        return;

    // open the file the filepath points to
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    KoOdfStylesReader stylesReader;
    KoOdfLoadingContext odfContext(stylesReader, nullptr);
    KoShapeLoadingContext shapeContext(odfContext, canvas()->shapeController()->resourceManager());

    // setup a DOM structure and start the actual loading process
    KoXmlDocument tmpDocument;
    tmpDocument.setContent(&file, false, nullptr, nullptr, nullptr);
    auto formulaElement = std::make_unique<FormulaElement>(); // create a new root element
    formulaElement->readMathML(tmpDocument.documentElement()); // and load the new formula
    FormulaCommand *command = new FormulaCommandLoad(m_formulaShape->formulaData(), std::move(formulaElement));
    canvas()->addCommand(new FormulaCommandUpdate(m_formulaShape, command));
}

void KoFormulaTool::saveFormula()
{
    const QString filePath = QFileDialog::getSaveFileName();
    if (filePath.isEmpty() || !shape())
        return;

    QFile file(filePath);
    KoXmlWriter writer(&file);
    KoGenStyles styles;
    KoEmbeddedDocumentSaver embeddedSaver;
    KoShapeSavingContext shapeSavingContext(writer, styles, embeddedSaver);

    m_formulaShape->formulaData()->saveMathML(shapeSavingContext);
}

void KoFormulaTool::setupActions()
{
    // notice that only empty mrows hows parent is a inferred mrow are treated as placeholders
    // this causes the <mrow><mrow/></mrow> constructs
    addTemplateAction(i18n("Insert fenced element"), "insert_fence", "<mfenced><mrow/></mfenced>", koIconNameCStr("brackets"));
    addTemplateAction(i18n("Insert enclosed element"), "insert_enclosed", "<menclosed><mrow/></menclosed>", koIconNameCStr("enclosed"));

    addTemplateAction(i18n("Insert root"), "insert_root", "<mroot><mrow><mrow/></mrow></mroot>", koIconNameCStr("root"));
    addTemplateAction(i18n("Insert square root"), "insert_sqrt", "<msqrt><mrow/></msqrt>", koIconNameCStr("sqrt"));

    addTemplateAction(i18n("Insert fraction"), "insert_fraction", "<mfrac><mrow><mrow/></mrow><mrow/></mfrac>", koIconNameCStr("frac"));
    addTemplateAction(i18n("Insert bevelled fraction"),
                      "insert_bevelled_fraction",
                      "<mfrac bevelled=\"true\"><mrow><mrow/></mrow><mrow/></mfrac>",
                      koIconNameCStr("bevelled"));

    addTemplateAction(i18n("Insert 3x3 table"),
                      "insert_33table",
                      "<mtable><mtr><mtd><mrow /></mtd><mtd></mtd><mtd></mtd></mtr>"
                      "<mtr><mtd></mtd><mtd></mtd><mtd></mtd></mtr>"
                      "<mtr><mtd></mtd><mtd></mtd><mtd></mtd></mtr></mtable>",
                      koIconNameCStr("matrix"));
    addTemplateAction(i18n("Insert 2 dimensional vector"),
                      "insert_21table",
                      "<mtable><mtr><mtd><mrow/></mtd></mtr><mtr><mtd></mtd></mtr></mtable>",
                      koIconNameCStr("vector"));

    addTemplateAction(i18n("Insert integral"),
                      "insert_integral",
                      "<msubsup><mo>&#x222B;</mo><mrow/><mrow/></msubsup>",
                      koIconNameCStr("int"));
    addTemplateAction(i18n("Insert large operator"),
                      "insert_large_operator",
                      "<munderover><mo>&#x2211;</mo><mrow/><mrow/></munderover>",
                      koIconNameCStr("sum"));
    addTemplateAction(i18n("Insert function"), "insert_function", "<mrow><mi>sin</mi><mrow/></mrow>", koIconNameCStr("func"));
    addTemplateAction(i18n("Insert accent"),
                      "insert_accent",
                      "<mover><mrow><mrow/></mrow><mo>&#x2D9;</mo></mover>",
                      koIconNameCStr("accent"));
    addTemplateAction(i18n("Insert operator"), "insert_operator", "<mrow><mo>&#x394;</mo><mrow/></mrow>", koIconNameCStr("operator"));

    addTemplateAction(i18n("Insert subscript"), "insert_subscript", "<msub><mrow><mrow/></mrow><mrow/></msub>", koIconNameCStr("rsub"));
    addTemplateAction(i18n("Insert superscript"), "insert_supscript", "<msup><mrow><mrow/></mrow><mrow/></msup>", koIconNameCStr("rsup"));
    addTemplateAction(i18n("Insert sub- and superscript"),
                      "insert_subsupscript",
                      "<msubsup><mrow><mrow/></mrow><mrow/><mrow/></msubsup>",
                      koIconNameCStr("rsubup"));
    addTemplateAction(i18n("Insert overscript"), "insert_overscript", "<mover><mrow><mrow/></mrow><mrow/></mover>", koIconNameCStr("gsup"));
    addTemplateAction(i18n("Insert underscript"), "insert_underscript", "<munder><mrow><mrow/></mrow><mrow/></munder>", koIconNameCStr("gsub"));
    addTemplateAction(i18n("Insert under- and overscript"),
                      "insert_underoverscript",
                      "<munderover><mrow><mrow/></mrow><mrow/><mrow/></munderover>",
                      koIconNameCStr("gsubup"));

    // only for debugging
    QAction *action;
    action = new QAction("Debug - writeElementTree", this);
    addAction("write_elementTree", action);

    QList<QVariant> list;
    action = new QAction(i18n("Insert row"), this);
    list << true << true;
    action->setData(list);
    list.clear();
    addAction("insert_row", action);
    action->setIcon(koIcon("insrow"));

    action = new QAction(i18n("Insert column"), this);
    list << false << true;
    action->setData(list);
    list.clear();
    addAction("insert_column", action);
    action->setIcon(koIcon("inscol"));

    action = new QAction(i18n("Remove row"), this);
    list << true << false;
    action->setData(list);
    list.clear();
    addAction("remove_row", action);
    action->setIcon(koIcon("remrow"));

    action = new QAction(i18n("Remove column"), this);
    list << false << false;
    action->setData(list);
    list.clear();
    addAction("remove_column", action);
    action->setIcon(koIcon("remcol"));
}

void KoFormulaTool::addTemplateAction(const QString &caption, const QString &name, const QString &data, const char *iconName)
{
    QAction *action = new QAction(caption, this);
    addAction(name, action);
    action->setIcon(QIcon::fromTheme(QLatin1String(iconName)));
    m_templateActions.push_back(TemplateAction{action, data});
    // the connection takes place when this KoToolBase is activated
}

void KoFormulaTool::copy() const
{
    QApplication::clipboard()->setText("test");
}

void KoFormulaTool::deleteSelection()
{
    KoToolBase::deleteSelection();
}

bool KoFormulaTool::paste()
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    if (data->hasFormat("text/plain")) {
        debugFormula << data->text();
        FormulaCommand *command = m_formulaEditor->insertText(data->text());
        if (command != nullptr) {
            canvas()->addCommand(new FormulaCommandUpdate(m_formulaShape, command));
        }
        repaintCursor();
        return true;
    }
    return false;
}

QStringList KoFormulaTool::supportedPasteMimeTypes() const
{
    QStringList tmp;
    tmp << "text/plain";
    tmp << "application/xml";
    return tmp;
}
