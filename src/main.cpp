#include "MainWindow.h"
#include "Updater.h"

#include <QApplication>
#include <QFutureWatcher>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QTimer>
#include <QtConcurrent>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DadIndustries"));
    QApplication::setApplicationName(QStringLiteral("DadsIPodTransferator9000"));
    QApplication::setApplicationDisplayName(QStringLiteral("Dad's iPod Transferator 9000"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("multimedia-player")));

    MainWindow window;
    window.show();

    QTimer::singleShot(1500, &window, [&window]() {
        auto *checkWatcher = new QFutureWatcher<UpdateCheckResult>(&window);
        QObject::connect(checkWatcher, &QFutureWatcher<UpdateCheckResult>::finished, &window,
                         [&window, checkWatcher]() {
            const UpdateCheckResult update = checkWatcher->result();
            checkWatcher->deleteLater();

            // Startup update checks are intentionally quiet when GitHub is unavailable.
            if (!update.error.isEmpty() || !update.updateAvailable) {
                return;
            }

            const auto answer = QMessageBox::question(
                &window,
                QStringLiteral("Update available"),
                QStringLiteral("Dad's iPod Transferator 9000 %1 is available.\n\n"
                               "You are running %2. Install the update now?")
                    .arg(update.latestVersion, update.currentVersion),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes);
            if (answer != QMessageBox::Yes) {
                return;
            }

            auto *progress = new QProgressDialog(
                QStringLiteral("Downloading and installing update…"),
                QString(), 0, 0, &window);
            progress->setWindowTitle(QStringLiteral("Updating"));
            progress->setWindowModality(Qt::WindowModal);
            progress->setCancelButton(nullptr);
            progress->setMinimumDuration(0);
            progress->show();

            auto *installWatcher = new QFutureWatcher<UpdateInstallResult>(&window);
            QObject::connect(installWatcher, &QFutureWatcher<UpdateInstallResult>::finished, &window,
                             [&window, installWatcher, progress, update]() {
                const UpdateInstallResult result = installWatcher->result();
                installWatcher->deleteLater();
                progress->close();
                progress->deleteLater();

                if (!result.success) {
                    QMessageBox::warning(
                        &window,
                        QStringLiteral("Update failed"),
                        QStringLiteral("The update could not be installed.\n\n%1")
                            .arg(result.error));
                    return;
                }

                const auto restart = QMessageBox::question(
                    &window,
                    QStringLiteral("Update installed"),
                    QStringLiteral("Version %1 was installed successfully. Restart now?")
                        .arg(update.latestVersion),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes);
                if (restart == QMessageBox::Yes) {
                    QProcess::startDetached(QCoreApplication::applicationFilePath());
                    QCoreApplication::quit();
                }
            });

            installWatcher->setFuture(QtConcurrent::run([sha = update.commitSha]() {
                return Updater::installUpdate(sha);
            }));
        });

        checkWatcher->setFuture(QtConcurrent::run([]() {
            return Updater::checkForUpdates();
        }));
    });

    return app.exec();
}
