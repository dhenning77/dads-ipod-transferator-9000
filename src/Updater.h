#pragma once

#include <QString>

struct UpdateCheckResult {
    bool updateAvailable = false;
    QString currentVersion;
    QString latestVersion;
    QString commitSha;
    QString error;
};

struct UpdateInstallResult {
    bool success = false;
    QString error;
};

class Updater {
public:
    static UpdateCheckResult checkForUpdates();
    static UpdateInstallResult installUpdate(const QString &commitSha);
    static QString currentVersion();
};
