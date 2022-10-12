#include "treesitterinspector.h"
#include "ui_treesitterinspector.h"

#include "transformpreviewdialog.h"

#include "treesitter/languages.h"
#include "treesitter/transformation.h"

#include <core/logger.h>
#include <core/lspdocument.h>
#include <core/project.h>

#include <spdlog/spdlog.h>

#include <QMessageBox>
#include <QTextEdit>

namespace Gui {

QueryErrorHighlighter::QueryErrorHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
    , m_errorUtf8Position(-1)
{
}

void QueryErrorHighlighter::highlightBlock(const QString &text)
{
    auto previousLength = previousBlockState();
    if (previousLength == -1) {
        previousLength = 0;
    }
    const auto utf8Text = text.toStdString();
    const auto newLength = previousLength + static_cast<int>(utf8Text.length()) + 1 /* the \n character */;
    setCurrentBlockState(newLength);

    if (m_errorUtf8Position != -1 && previousLength <= m_errorUtf8Position && m_errorUtf8Position < newLength) {
        // error is in this block
        auto innerPosition = m_errorUtf8Position - previousLength;

        // get the error position in UTF-16
        auto textBeforeError = QString::fromUtf8(utf8Text.substr(0, innerPosition));
        QTextCharFormat errorFormat;
        errorFormat.setFontWeight(QFont::Bold);
        errorFormat.setFontUnderline(true);
        errorFormat.setUnderlineColor(QColorConstants::Red);
        errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);

        // Unfortunately, TreeSitter doesn't tell us how "long" the error is, only where it starts.
        // So extend the highlight to span one full "keyword".
        //
        // Regex to find the first non-alphanumeric character.
        static QRegularExpression regex("[^\\w]");
        auto to = text.indexOf(regex, textBeforeError.size());
        if (to == -1) {
            to = text.size();
        }
        auto count = to - textBeforeError.size();
        if (count <= 0) {
            count = 1;
        }

        setFormat(textBeforeError.size(), count, errorFormat);
    }
}

void QueryErrorHighlighter::setUtf8Position(int position)
{
    m_errorUtf8Position = position;
    rehighlight();
}

TreeSitterInspector::TreeSitterInspector(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TreeSitterInspector)
    , m_parser(tree_sitter_cpp())
    , m_errorHighlighter(nullptr)
    , m_document(nullptr)
{
    ui->setupUi(this);
    m_errorHighlighter = new QueryErrorHighlighter(ui->query->document());

    connect(Core::Project::instance(), &Core::Project::currentDocumentChanged, this,
            &TreeSitterInspector::currentDocumentChanged);

    ui->treeInspector->setModel(&m_treemodel);

    connect(ui->treeInspector->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &TreeSitterInspector::treeSelectionChanged);

    connect(ui->query, &QPlainTextEdit::textChanged, this, &TreeSitterInspector::queryChanged);

    currentDocumentChanged();

    connect(ui->previewButton, &QPushButton::clicked, this, &TreeSitterInspector::previewTransformation);
    connect(ui->runButton, &QPushButton::clicked, this, &TreeSitterInspector::runTransformation);
}

TreeSitterInspector::~TreeSitterInspector()
{
    delete ui;
}

void TreeSitterInspector::queryStateChanged()
{
    if (m_treemodel.hasQuery()) {
        int patternCount = m_treemodel.patternCount();
        int matchCount = m_treemodel.matchCount();

        ui->queryInfo->setText(tr("<span style='color:%1'>%2 Patterns - %3 Matches - %4 Captures</span>")
                                   .arg(patternCount == 0 || matchCount == 0 ? "yellow" : "green")
                                   .arg(patternCount)
                                   .arg(matchCount)
                                   .arg(m_treemodel.captureCount()));
    }
}

void TreeSitterInspector::currentDocumentChanged()
{
    setDocument(qobject_cast<Core::LspDocument *>(Core::Project::instance()->currentDocument()));
}

void TreeSitterInspector::queryChanged()
{
    const auto text = ui->query->toPlainText();
    // rehighlight causes another textChanged signal to be emitted.
    // So check whether any characters actually changed.
    if (m_queryText == text) {
        return;
    }
    m_queryText = text;

    if (text.isEmpty()) {
        m_treemodel.setQuery({});
        ui->queryInfo->setText("");
        m_errorHighlighter->setUtf8Position(-1);
        return;
    }

    try {
        treesitter::Query query(tree_sitter_cpp(), ui->query->toPlainText());
        m_treemodel.setQuery(std::move(query));
        m_errorHighlighter->setUtf8Position(-1);

        queryStateChanged();
    } catch (treesitter::Query::Error error) {
        m_treemodel.setQuery({});
        ui->queryInfo->setText(highlightQueryError(error));

        // The error may be behind the last character, which couldn't be highlighted
        // So move back by one character in that case.
        if (error.utf8_offset == text.toStdString().size()) {
            error.utf8_offset--;
        }
        m_errorHighlighter->setUtf8Position(error.utf8_offset);
    }
}

void TreeSitterInspector::textChanged()
{
    QString text;
    {
        Core::LoggerDisabler disableLogging;
        text = m_document->text();
    }
    auto tree = m_parser.parseString(text);
    if (tree.has_value()) {
        m_treemodel.setTree(std::move(tree.value()));
        ui->treeInspector->expandAll();
        for (int i = 0; i < 2; i++) {
            ui->treeInspector->resizeColumnToContents(i);
        }
        queryStateChanged();
    } else {
        m_treemodel.clear();
    }
}

void TreeSitterInspector::cursorChanged()
{
    int position;
    {
        Core::LoggerDisabler disableLogging;
        position = m_document->position();
    }
    m_treemodel.setCursorPosition(position);
}

void TreeSitterInspector::setDocument(Core::LspDocument *document)
{
    if (m_document) {
        disconnect(m_document);
    }

    m_document = document;
    if (m_document) {
        connect(m_document, &Core::LspDocument::textChanged, this, &TreeSitterInspector::textChanged);
        connect(m_document, &Core::LspDocument::positionChanged, this, &TreeSitterInspector::cursorChanged);

        cursorChanged();
        textChanged();
    } else {
        m_treemodel.clear();
    }
}

QString TreeSitterInspector::preCheckTransformation() const
{
    if (m_document == nullptr) {
        return tr("You need to open a C++ document!");
    }

    if (m_queryText.isEmpty()) {
        return tr("You need to specify a query!");
    }

    if (ui->target->toPlainText().isEmpty()) {
        return tr("You need to specify a transformation target!");
    }

    return QString();
}

void TreeSitterInspector::previewTransformation()
{
    prepareTransformation([this](auto &transformation) {
        auto result = transformation.run();

        TransformPreviewDialog dialog(m_document, result, transformation.replacementsMade(), this);
        if (dialog.exec() == QDialog::Accepted) {
            m_document->setText(result);
        }
    });
}

QString TreeSitterInspector::highlightQueryError(const treesitter::Query::Error &error) const
{
    return tr("<span style='color:red'><b>%1</b> at character: %2</span>")
        .arg(error.description)
        .arg(error.utf8_offset);
}

void TreeSitterInspector::runTransformation()
{
    prepareTransformation([this](auto &transformation) {
        m_document->setText(transformation.run());

        QMessageBox msgBox;
        msgBox.setText(tr("%1 Replacements made").arg(transformation.replacementsMade()));
    });
}

void TreeSitterInspector::prepareTransformation(
    std::function<void(treesitter::Transformation &transformation)> runFunction)
{
    const auto errorMessage = preCheckTransformation();
    if (!errorMessage.isEmpty()) {
        QMessageBox msgBox;
        msgBox.setText(errorMessage);
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.exec();
        return;
    }

    try {
        treesitter::Query query(tree_sitter_cpp(), m_queryText);
        treesitter::Parser parser(tree_sitter_cpp());

        treesitter::Transformation transformation(m_document->text(), std::move(parser), std::move(query),
                                                  ui->target->toPlainText());

        runFunction(transformation);
    } catch (treesitter::Query::Error error) {
        QMessageBox msgBox;
        msgBox.setText(tr("Error in Query"));
        msgBox.setInformativeText(highlightQueryError(error));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.exec();
        return;
    } catch (treesitter::Transformation::Error error) {
        QMessageBox msgBox;
        msgBox.setText(tr("Error performing Transformation"));
        msgBox.setInformativeText(error.description);
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.exec();
        return;
    }
}

void TreeSitterInspector::treeSelectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    const auto node = m_treemodel.tsNode(current);
    if (node.has_value()) {
        m_document->selectRegion(node->startPosition(), node->endPosition());
    }
}

} // namespace Gui
