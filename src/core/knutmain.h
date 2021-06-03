#pragma once

#include <QCommandLineParser>
#include <QObject>

class QCoreApplication;

namespace Core {

class KnutMain : public QObject
{
    Q_OBJECT

public:
    explicit KnutMain(QObject *parent = nullptr);

    void process(const QCoreApplication &app);
    void process(const QStringList &arguments);
};

} // namespace Core
