#include "DiagnosticLogger.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfoList>
#include <QtCore/QMessageLogContext>
#include <QtCore/QMutexLocker>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringList>

namespace {

constexpr int kLogsToKeep = 12;

QString fallbackLogDirectory()
{
    return QDir::home().filePath(QStringLiteral("NeoLDG/logs"));
}

QString qtMessageLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }

    return QStringLiteral("QT");
}

} // namespace

DiagnosticLogger::~DiagnosticLogger()
{
    shutdown();
}

DiagnosticLogger& DiagnosticLogger::instance()
{
    static DiagnosticLogger logger;
    return logger;
}

void DiagnosticLogger::initialize()
{
    QMutexLocker locker(&mutex_);
    if (initialized_) {
        return;
    }

    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QDir::home().filePath(QStringLiteral("NeoLDG"));
    }

    QDir appDataDir(appDataPath);
    if (!appDataDir.mkpath(QStringLiteral("logs"))) {
        logDirectoryPath_ = fallbackLogDirectory();
        QDir().mkpath(logDirectoryPath_);
    } else {
        logDirectoryPath_ = appDataDir.filePath(QStringLiteral("logs"));
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    logFilePath_ = QDir(logDirectoryPath_).filePath(QStringLiteral("NeoLDG-%1.log").arg(timestamp));

    file_.setFileName(logFilePath_);
    if (file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        stream_.setDevice(&file_);
        initialized_ = true;
        previousHandler_ = qInstallMessageHandler(&DiagnosticLogger::qtMessageHandler);
    }

    locker.unlock();

    pruneOldLogs();
    info(QStringLiteral("app"), QStringLiteral("NeoLDG %1 started. Qt %2. Log file: %3")
        .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()), logFilePath_));
}

void DiagnosticLogger::shutdown()
{
    {
        QMutexLocker locker(&mutex_);
        if (!initialized_) {
            return;
        }
    }

    info(QStringLiteral("app"), QStringLiteral("Diagnostic logger shutting down."));

    QMutexLocker locker(&mutex_);
    if (!initialized_) {
        return;
    }

    qInstallMessageHandler(previousHandler_);
    previousHandler_ = nullptr;
    stream_.flush();
    file_.flush();
    file_.close();
    stream_.setDevice(nullptr);
    initialized_ = false;
}

void DiagnosticLogger::setVerboseEnabled(bool enabled)
{
    {
        QMutexLocker locker(&mutex_);
        if (verboseEnabled_ == enabled) {
            return;
        }
        verboseEnabled_ = enabled;
    }

    info(QStringLiteral("diagnostics"), enabled
        ? QStringLiteral("Verbose diagnostic logging enabled.")
        : QStringLiteral("Verbose diagnostic logging disabled."));
}

bool DiagnosticLogger::isVerboseEnabled() const
{
    QMutexLocker locker(&mutex_);
    return verboseEnabled_;
}

QString DiagnosticLogger::logFilePath() const
{
    QMutexLocker locker(&mutex_);
    return logFilePath_;
}

QString DiagnosticLogger::logDirectoryPath() const
{
    QMutexLocker locker(&mutex_);
    return logDirectoryPath_;
}

void DiagnosticLogger::info(const QString& category, const QString& message)
{
    writeLine(QStringLiteral("INFO"), category, message, true);
}

void DiagnosticLogger::warning(const QString& category, const QString& message)
{
    writeLine(QStringLiteral("WARN"), category, message, true);
}

void DiagnosticLogger::error(const QString& category, const QString& message)
{
    writeLine(QStringLiteral("ERROR"), category, message, true);
}

void DiagnosticLogger::verbose(const QString& category, const QString& message)
{
    writeLine(QStringLiteral("VERBOSE"), category, message, false);
}

void DiagnosticLogger::serialBytes(const QString& direction, const QByteArray& bytes)
{
    if (!isVerboseEnabled()) {
        return;
    }

    verbose(QStringLiteral("serial"), QStringLiteral("%1 %2 bytes: %3")
        .arg(direction)
        .arg(bytes.size())
        .arg(hexDump(bytes)));
}

void DiagnosticLogger::qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QString text = message;
    if (context.file != nullptr && context.line > 0) {
        text += QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line);
    }

    instance().writeLine(qtMessageLevel(type), QStringLiteral("qt"), text, true);
}

void DiagnosticLogger::writeLine(const QString& level, const QString& category, const QString& message, bool force)
{
    QMutexLocker locker(&mutex_);
    if (!initialized_ || (!force && !verboseEnabled_)) {
        return;
    }

    stream_
        << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        << " [" << level << "]"
        << " [" << category << "] "
        << message
        << '\n';
    stream_.flush();
    file_.flush();
}

void DiagnosticLogger::pruneOldLogs()
{
    if (logDirectoryPath_.isEmpty()) {
        return;
    }

    const QFileInfoList logs = QDir(logDirectoryPath_).entryInfoList(
        {QStringLiteral("NeoLDG-*.log")},
        QDir::Files,
        QDir::Time);

    for (int i = kLogsToKeep; i < logs.size(); ++i) {
        QFile::remove(logs.at(i).absoluteFilePath());
    }
}

QString DiagnosticLogger::hexDump(const QByteArray& bytes)
{
    QStringList parts;
    parts.reserve(bytes.size());
    for (const char byte : bytes) {
        parts.append(QString::number(static_cast<unsigned char>(byte), 16)
            .toUpper()
            .rightJustified(2, QChar('0')));
    }
    return parts.join(QLatin1Char(' '));
}
