#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "rctoqrcdialog.h"
#include "rctouidialog.h"
#include "runscriptdialog.h"
#include "settingsdialog.h"
#include "uiview.h"

#include "core/document.h"
#include "core/project.h"
#include "core/rcdocument.h"
#include "core/textdocument.h"
#include "core/uidocument.h"
#include "rcui/rcfileview.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QSettings>

namespace Gui {

constexpr int MaximumRecentProjects = 10;
constexpr char RecentProjectKey[] = "RecentProject";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    setAttribute(Qt::WA_DeleteOnClose);

    ui->setupUi(this);
    ui->splitter->setSizes({275, 749});
    setWindowTitle(QApplication::applicationName() + ' ' + QApplication::applicationVersion());

    connect(ui->action_Quit, &QAction::triggered, this, &MainWindow::close);
    connect(ui->action_Open, &QAction::triggered, this, &MainWindow::openProject);
    connect(ui->actionCreate_Qrc, &QAction::triggered, this, &MainWindow::createQrc);
    connect(ui->actionCreate_Ui, &QAction::triggered, this, &MainWindow::createUi);
    connect(ui->action_Run_Script, &QAction::triggered, this, &MainWindow::runScript);
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::openSettings);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveDocument);
    connect(ui->actionSaveAll, &QAction::triggered, this, &MainWindow::saveAllDocuments);

    m_recentProjects = new QMenu(this);
    ui->actionRecent_Projects->setMenu(m_recentProjects);
    updateRecentProjects();

    ui->tabWidget->setDocumentMode(true);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::changeTab);

    m_fileModel = new QFileSystemModel(this);
    ui->projectView->setModel(m_fileModel);
    for (int i = 1; i < m_fileModel->columnCount(); ++i)
        ui->projectView->header()->hideSection(i);

    auto project = Core::Project::instance();
    connect(project, &Core::Project::currentDocumentChanged, this, &MainWindow::changeCurrentDocument);

    auto path = project->root();
    if (!path.isEmpty())
        initProject(path);
    if (project->currentDocument())
        changeCurrentDocument();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::openProject()
{
    auto path = QFileDialog::getExistingDirectory(this, "Open project", QDir::currentPath());
    if (!path.isEmpty()) {
        Core::Project::instance()->setRoot(path);
        initProject(path);
    }
}

void MainWindow::initProject(const QString &path)
{
    // Update recent list
    QSettings settings;
    QStringList projects = settings.value(RecentProjectKey).toStringList();
    projects.removeAll(path);
    projects.prepend(path);
    while (projects.size() > MaximumRecentProjects)
        projects.removeLast();
    settings.setValue(RecentProjectKey, projects);

    // Initalize tree view
    auto index = m_fileModel->setRootPath(path);
    ui->projectView->setRootIndex(index);
    connect(ui->projectView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::openDocument);

    // Disable menus, we can only load one project - restart Knut if needed
    ui->action_Open->setEnabled(false);
    ui->actionRecent_Projects->setEnabled(false);
}

void MainWindow::updateRecentProjects()
{
    QSettings settings;
    const QStringList projects = settings.value(RecentProjectKey).toStringList();

    const int numRecentProjects = qMin(projects.count(), MaximumRecentProjects);
    m_recentProjects->clear();
    for (int i = 0; i < numRecentProjects; ++i) {
        const QString path = projects.value(i);
        QAction *act = m_recentProjects->addAction(path);
        connect(act, &QAction::triggered, this, [this, path]() {
            Core::Project::instance()->setRoot(path);
            initProject(path);
        });
    }
    ui->actionRecent_Projects->setEnabled(numRecentProjects > 0);
}

void MainWindow::openDocument(const QModelIndex &index)
{
    auto path = m_fileModel->filePath(index);
    QFileInfo fi(path);
    if (fi.isFile())
        Core::Project::instance()->open(path);
}

void MainWindow::saveAllDocuments()
{
    Core::Project::instance()->saveAllDocuments();
}

void MainWindow::saveDocument()
{
    auto document = Core::Project::instance()->currentDocument();
    if (document)
        document->save();
}

void MainWindow::createQrc()
{
    auto document = Core::Project::instance()->currentDocument();
    if (auto rcDocument = qobject_cast<Core::RcDocument *>(document)) {
        RcToQrcDialog dialog(rcDocument, this);
        dialog.exec();
    }
}

void MainWindow::createUi()
{
    auto document = Core::Project::instance()->currentDocument();
    if (auto rcDocument = qobject_cast<Core::RcDocument *>(document)) {
        RcToUiDialog dialog(rcDocument, this);
        dialog.exec();
    }
}

void MainWindow::runScript()
{
    RunScriptDialog dialog(this);
    dialog.exec();
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::changeTab()
{
    auto document = Core::Project::instance()->open(ui->tabWidget->currentWidget()->windowTitle());
    if (!document)
        return;

    ui->actionCreate_Qrc->setEnabled(document->type() == Core::Document::Type::Rc);
    ui->actionCreate_Ui->setEnabled(document->type() == Core::Document::Type::Rc);
}

static QWidget *widgetForDocument(Core::Document *document)
{
    switch (document->type()) {
    case Core::Document::Type::Cpp:
    case Core::Document::Type::Text: {
        auto textEdit = qobject_cast<Core::TextDocument *>(document)->textEdit();
        auto f = textEdit->font();
        f.setFamily(QStringLiteral("Courier New"));
        f.setPointSize(10);
        textEdit->setFont(f);
        return textEdit;
    }
    case Core::Document::Type::Rc: {
        auto rcview = new RcUi::RcFileView();
        rcview->setRcFile(qobject_cast<Core::RcDocument *>(document)->data());
        return rcview;
    }
    case Core::Document::Type::Ui:
        auto uiview = new UiView();
        uiview->setUiDocument(qobject_cast<Core::UiDocument *>(document));
        return uiview;
    }
    Q_UNREACHABLE();
    return nullptr;
}

static void updateTabTitle(QTabWidget *tabWidget, int index, bool hasChanged)
{
    QString text = tabWidget->tabText(index);
    if (hasChanged && !text.endsWith('*'))
        text += '*';
    else if (!hasChanged && text.endsWith('*'))
        text.remove(text.size() - 1, 1);
    tabWidget->setTabText(index, text);
}

void MainWindow::changeCurrentDocument()
{
    auto project = Core::Project::instance();
    const QString fileName = project->currentDocument()->fileName();

    // open the window if it's already opened
    int windowIndex = m_windows.value(fileName, -1);
    if (windowIndex == -1) {
        auto document = project->currentDocument();
        QDir dir(project->root());
        auto widget = widgetForDocument(document);
        widget->setWindowTitle(fileName);
        windowIndex = ui->tabWidget->addTab(widget, dir.relativeFilePath(fileName));
        m_windows[fileName] = windowIndex;

        connect(document, &Core::Document::hasChangedChanged, this, [this, windowIndex, document]() {
            updateTabTitle(ui->tabWidget, windowIndex, document->hasChanged());
        });
    }
    ui->tabWidget->setCurrentIndex(windowIndex);

    const QModelIndex &index = m_fileModel->index(fileName);
    ui->projectView->setCurrentIndex(index);
}

} // namespace Gui
