#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QMutex>
#include <QtCore/QString>
#include <QtCore/QTextStream>
#include <QtCore/QtGlobal>

class DiagnosticLogger
{
public:
    static DiagnosticLogger& instance();

    void initialize();
    void shutdown();

    void setVerboseEnabled(bool enabled);
    bool isVerboseEnabled() const;

    QString logFilePath() const;
    QString logDirectoryPath() const;

    void info(const QString& category, const QString& message);
    void warning(const QString& category, const QString& message);
    void error(const QString& category, const QString& message);
    void verbose(const QString& category, const QString& message);
    void serialBytes(const QString& direction, const QByteArray& bytes);

private:
    DiagnosticLogger() = default;
    ~DiagnosticLogger();

    DiagnosticLogger(const DiagnosticLogger&) = delete;
    DiagnosticLogger& operator=(const DiagnosticLogger&) = delete;

    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);

    void writeLine(const QString& level, const QString& category, const QString& message, bool force);
    void pruneOldLogs();

    static QString hexDump(const QByteArray& bytes);

    mutable QMutex mutex_;
    QFile file_;
    QTextStream stream_;
    QString logFilePath_;
    QString logDirectoryPath_;
    QtMessageHandler previousHandler_ = nullptr;
    bool initialized_ = false;
    bool verboseEnabled_ = false;
};
