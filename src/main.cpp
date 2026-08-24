#include "MainWindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DadIndustries"));
    QApplication::setApplicationName(QStringLiteral("DadsIPodTransferator9000"));
    QApplication::setApplicationDisplayName(QStringLiteral("Dad's iPod Transferator 9000"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("multimedia-player")));

    MainWindow window;
    window.show();
    return app.exec();
}
