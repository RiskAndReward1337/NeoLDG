#include "MainWindow.hpp"

#include "DiagnosticLogger.hpp"

#include <QtCore/QMetaType>
#include <QtCore/QObject>
#include <QtGui/QFont>
#include <QtGui/QFontInfo>
#include <QtWidgets/QApplication>

#include <cstdlib>
#include <exception>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NeoLDG"));
    app.setApplicationDisplayName(QStringLiteral("NeoLDG"));
    app.setOrganizationName(QStringLiteral("NeoLDG"));
    app.setApplicationVersion(QStringLiteral(NEOLDG_VERSION));
    QApplication::setStyle(QStringLiteral("Fusion"));

    DiagnosticLogger::instance().initialize();
    std::set_terminate([]() {
        DiagnosticLogger::instance().error(QStringLiteral("app"), QStringLiteral("std::terminate called. The application is aborting."));
        DiagnosticLogger::instance().shutdown();
        std::abort();
    });
    QObject::connect(&app, &QApplication::aboutToQuit, []() {
        DiagnosticLogger::instance().info(QStringLiteral("app"), QStringLiteral("NeoLDG shutting down normally."));
    });

    QFont font(QStringLiteral("IBM Plex Sans"));
    if (!QFontInfo(font).family().contains(QStringLiteral("IBM Plex"), Qt::CaseInsensitive)) {
        font = QFont(QStringLiteral("Noto Sans"));
    }
    font.setPointSize(10);
    app.setFont(font);

    qRegisterMetaType<neoldg::MeterSample>();
    qRegisterMetaType<neoldg::DisplayTelemetry>();
    qRegisterMetaType<neoldg::TuneOutcome>();
    qRegisterMetaType<neoldg::CommandResult>();

    MainWindow window;
    window.show();
    const int result = app.exec();
    DiagnosticLogger::instance().shutdown();
    return result;
}
