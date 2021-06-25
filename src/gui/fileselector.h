#pragma once

#include <QWidget>

class QLineEdit;

namespace Gui {

class FileSelector : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName)

public:
    enum class Mode { OpenFile, OpenDirectory, SaveFile };

public:
    explicit FileSelector(QWidget *parent = nullptr);

    QString fileName() const;
    Mode mode() const;

public slots:
    void setFilter(const QString &filter);
    void setFileName(const QString &fileName);
    void setMode(Mode mode);

signals:
    void fileNameChanged(const QString &fileName);

private:
    void chooseFile();

private:
    QLineEdit *m_lineEdit = nullptr;
    QString m_filter;
    Mode m_mode = Mode::OpenFile;
};

} // namespace Gui