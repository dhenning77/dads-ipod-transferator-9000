#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <functional>

struct TrackInfo {
    quint64 dbid = 0;
    QString title;
    QString artist;
    QString album;
    int trackNumber = 0;
    int durationMs = 0;
    quint64 sizeBytes = 0;
};

struct AddResult {
    int added = 0;
    int skippedDuplicates = 0;
    QStringList errors;
    QString backupPath;
};

struct RemoveResult {
    int removed = 0;
    QStringList errors;
    QString backupPath;
};

class IpodBackend {
public:
    using ProgressCallback = std::function<void(int current, int total, const QString &message)>;

    IpodBackend();
    ~IpodBackend();

    bool ensureMounted(QString *error = nullptr);
    bool unmount(QString *error = nullptr, bool forceEvenIfNotOwned = false);
    bool isMounted() const;

    QString deviceName() const { return m_deviceName; }
    QString udid() const { return m_udid; }
    QString mountPoint() const { return m_mountPoint; }

    QVector<TrackInfo> loadTracks(QString *error = nullptr);
    AddResult addTracks(const QStringList &files, const ProgressCallback &progress = {});
    RemoveResult removeTracks(const QList<quint64> &dbids, const ProgressCallback &progress = {});

    quint64 bytesAvailable() const;
    quint64 bytesTotal() const;

private:
    bool discoverDevice(QString *error);
    bool verifyDeviceMetadata(QString *error) const;
    QString backupDatabase(QString *error) const;
    bool run(const QString &program, const QStringList &args,
             QString *stdoutText = nullptr, QString *stderrText = nullptr,
             int timeoutMs = 30000) const;

    QString m_udid;
    QString m_deviceName;
    QString m_mountPoint;
    bool m_ownsMount = false;
};
