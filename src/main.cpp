#include "MainWindow.hpp"

#include <QtCore/QMetaType>
#include <QtGui/QFont>
#include <QtGui/QFontInfo>
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NeoLDG"));
    app.setApplicationDisplayName(QStringLiteral("NeoLDG"));
    app.setOrganizationName(QStringLiteral("NeoLDG"));
    QApplication::setStyle(QStringLiteral("Fusion"));

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
    return app.exec();
}
