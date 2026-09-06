#include "Updater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

namespace {

constexpr auto kCommitUrl = "https://api.github.com/repos/dhenning77/dads-ipod-transferator-9000/commits/main";

bool run(const QString &program, const QStringList &arguments, QByteArray *output,
         QString *error, int timeoutMs = 120000, const QString &workingDirectory = {}) {
    QProcess process;
    if (!workingDirectory.isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        if (error) {
            *error = QStringLiteral("Could not start %1.").arg(program);
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        if (error) {
            *error = QStringLiteral("%1 timed out.").arg(program);
        }
        return false;
    }

    if (output) {
        *output = process.readAllStandardOutput();
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
            *error = stderrText.isEmpty()
                ? QStringLiteral("%1 exited with code %2.").arg(program).arg(process.exitCode())
                : stderrText;
        }
        return false;
    }
    return true;
}

QList<int> versionParts(const QString &version) {
    QList<int> parts;
    const QStringList strings = version.trimmed().split(QLatin1Char('.'));
    for (const QString &part : strings) {
        bool ok = false;
        const int value = part.toInt(&ok);
        if (!ok) {
            return {};
        }
        parts << value;
    }
    return parts;
}

bool isNewerVersion(const QString &candidate, const QString &current) {
    QList<int> a = versionParts(candidate);
    QList<int> b = versionParts(current);
    if (a.isEmpty() || b.isEmpty()) {
        return candidate.trimmed() != current.trimmed();
    }
    const int count = qMax(a.size(), b.size());
    while (a.size() < count) a << 0;
    while (b.size() < count) b << 0;
    for (int i = 0; i < count; ++i) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return false;
}

} // namespace

QString Updater::currentVersion() {
    return QCoreApplication::applicationVersion();
}

UpdateCheckResult Updater::checkForUpdates() {
    UpdateCheckResult result;
    result.currentVersion = currentVersion();

    QByteArray commitOutput;
    if (!run(QStringLiteral("curl"),
             {QStringLiteral("-fsSL"), QString::fromLatin1(kCommitUrl)},
             &commitOutput, &result.error, 30000)) {
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(commitOutput, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("Could not parse the GitHub update response.");
        return result;
    }

    result.commitSha = document.object().value(QStringLiteral("sha")).toString();
    if (result.commitSha.isEmpty()) {
        result.error = QStringLiteral("GitHub did not return a commit SHA for main.");
        return result;
    }

    const QString versionUrl = QStringLiteral(
        "https://raw.githubusercontent.com/dhenning77/dads-ipod-transferator-9000/%1/VERSION")
        .arg(result.commitSha);
    QByteArray versionOutput;
    if (!run(QStringLiteral("curl"),
             {QStringLiteral("-fsSL"), versionUrl},
             &versionOutput, &result.error, 30000)) {
        return result;
    }

    result.latestVersion = QString::fromUtf8(versionOutput).trimmed();
    if (result.latestVersion.isEmpty()) {
        result.error = QStringLiteral("GitHub returned an empty VERSION file.");
        return result;
    }

    result.updateAvailable = isNewerVersion(result.latestVersion, result.currentVersion);
    return result;
}

UpdateInstallResult Updater::installUpdate(const QString &commitSha) {
    UpdateInstallResult result;
    if (commitSha.isEmpty()) {
        result.error = QStringLiteral("No update commit was provided.");
        return result;
    }

    QTemporaryDir tempDir(QDir::tempPath() + QStringLiteral("/dads-ipod-update-XXXXXX"));
    if (!tempDir.isValid()) {
        result.error = QStringLiteral("Could not create a temporary update directory.");
        return result;
    }

    const QString archive = QDir(tempDir.path()).filePath(QStringLiteral("update.tar.gz"));
    const QString archiveUrl = QStringLiteral(
        "https://codeload.github.com/dhenning77/dads-ipod-transferator-9000/tar.gz/%1")
        .arg(commitSha);

    if (!run(QStringLiteral("curl"),
             {QStringLiteral("-fL"), QStringLiteral("--retry"), QStringLiteral("2"),
              QStringLiteral("-o"), archive, archiveUrl},
             nullptr, &result.error, 120000)) {
        return result;
    }

    const QString sourceDir = QDir(tempDir.path()).filePath(QStringLiteral("source"));
    QDir().mkpath(sourceDir);
    if (!run(QStringLiteral("tar"),
             {QStringLiteral("-xzf"), archive, QStringLiteral("-C"), sourceDir,
              QStringLiteral("--strip-components=1")},
             nullptr, &result.error, 60000)) {
        return result;
    }

    const QString installer = QDir(sourceDir).filePath(QStringLiteral("install.sh"));
    if (!QFile::exists(installer)) {
        result.error = QStringLiteral("The downloaded update does not contain install.sh.");
        return result;
    }

    if (!run(QStringLiteral("bash"), {installer}, nullptr, &result.error, 600000, sourceDir)) {
        return result;
    }

    result.success = true;
    return result;
}
