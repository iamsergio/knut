#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>

#include <spdlog/spdlog.h>

#include <memory>
#include <vector>

namespace Test {

inline QString testDataPath()
{
    QString path;
#if defined(TEST_DATA_PATH)
    path = TEST_DATA_PATH;
#endif
    if (path.isEmpty() || !QDir(path).exists()) {
        path = QCoreApplication::applicationDirPath() + "/test_data";
    }
    return path;
}

inline bool compareFiles(const QString &fileName1, const QString &fileName2)
{
    QFile file1(fileName1);
    if (!file1.open(QIODevice::ReadOnly))
        return false;
    QFile file2(fileName2);
    if (!file2.open(QIODevice::ReadOnly))
        return false;

    return file1.readAll() == file2.readAll();
}

class LogSilencer
{
public:
    inline static std::string Default = "";

    LogSilencer(const std::string &name = Default)
    {
        if (name.empty())
            m_logger = spdlog::default_logger();
        else
            m_logger = spdlog::get(name);
        if (m_logger) {
            m_level = m_logger->level();
            m_logger->set_level(spdlog::level::off);
        }
    }
    ~LogSilencer()
    {
        if (m_logger) {
            m_logger->set_level(m_level);
        }
    }

    LogSilencer(LogSilencer &&) noexcept = default;
    LogSilencer &operator=(LogSilencer &&) noexcept = default;

private:
    spdlog::level::level_enum m_level = spdlog::level::off;
    std::shared_ptr<spdlog::logger> m_logger;
};

class LogSilencers
{
public:
    LogSilencers(std::initializer_list<std::string> names)
    {
        m_logs.reserve(names.size());
        for (const auto &name : names)
            m_logs.emplace_back(name);
    }

private:
    std::vector<LogSilencer> m_logs;
};

constexpr inline bool noClangd()
{
#if defined(NO_CLANGD)
    return true;
#else
    return false;
#endif
}
}

// Check if clangd is available, needed for some tests
#define CHECK_CLANGD                                                                                                   \
    do {                                                                                                               \
        if constexpr (Test::noClangd())                                                                                \
            QSKIP("clangd is not available to run the tests");                                                         \
    } while (false)
