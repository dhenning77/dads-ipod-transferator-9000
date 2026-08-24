#include "IpodBackend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>

namespace {

QString decode64(const QByteArray &value) {
    return QString::fromUtf8(QByteArray::fromBase64(value));
}

QString helperPath() {
    const QString besideApp = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("dads-ipod-transferator-9000-helper"));
    if (QFileInfo::exists(besideApp)) {
        return besideApp;
    }
    return QStandardPaths::findExecutable(QStringLiteral("dads-ipod-transferator-9000-helper"));
}

QString errorFromHelper(const QByteArray &stderrData, const QString &fallback) {
    const QList<QByteArray> lines = stderrData.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("ERROR\t")) {
            return decode64(line.mid(6));
        }
    }
    const QString raw = QString::fromUtf8(stderrData).trimmed();
    return raw.isEmpty() ? fallback : raw;
}

void parseProgressLines(QByteArray &buffer, const IpodBackend::ProgressCallback &progress) {
    for (;;) {
        const qsizetype newline = buffer.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (!line.startsWith("P\t") || !progress) {
            continue;
        }
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() >= 4) {
            progress(fields[1].toInt(), fields[2].toInt(), decode64(fields[3]));
        }
    }
}

bool runHelper(const QStringList &args, QByteArray *stdoutData, QByteArray *stderrData,
               const IpodBackend::ProgressCallback &progress = {}, int timeoutMs = 300000) {
    const QString helper = helperPath();
    if (helper.isEmpty()) {
        if (stderrData) {
            *stderrData = QByteArrayLiteral("Dad's iPod Transferator 9000 helper is missing.");
        }
        return false;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(helper, args);
    if (!process.waitForStarted(5000)) {
        if (stderrData) {
            *stderrData = QStringLiteral("Could not start %1").arg(helper).toUtf8();
        }
        return false;
    }

    QByteArray output;
    QByteArray errors;
    QByteArray progressBuffer;
    QElapsedTimer timer;
    timer.start();

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        const QByteArray chunk = process.readAllStandardOutput();
        if (!chunk.isEmpty()) {
            output += chunk;
            progressBuffer += chunk;
            parseProgressLines(progressBuffer, progress);
        }
        errors += process.readAllStandardError();

        if (timeoutMs > 0 && timer.elapsed() > timeoutMs) {
            process.kill();
            process.waitForFinished(5000);
            errors += QByteArrayLiteral("\nHelper timed out.");
            break;
        }
    }

    const QByteArray finalChunk = process.readAllStandardOutput();
    output += finalChunk;
    progressBuffer += finalChunk;
    errors += process.readAllStandardError();
    parseProgressLines(progressBuffer, progress);

    if (stdoutData) {
        *stdoutData = output;
    }
    if (stderrData) {
        *stderrData = errors;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace

IpodBackend::IpodBackend()
    : m_mountPoint(QDir::home().filePath(QStringLiteral("ipod"))) {
    QDir().mkpath(m_mountPoint);
}

IpodBackend::~IpodBackend() {
    QString ignored;
    unmount(&ignored, false);
}

bool IpodBackend::run(const QString &program, const QStringList &args,
                      QString *stdoutText, QString *stderrText,
                      int timeoutMs) const {
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(5000)) {
        if (stderrText) {
            *stderrText = QStringLiteral("Could not start %1").arg(program);
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        if (stderrText) {
            *stderrText = QStringLiteral("%1 timed out").arg(program);
        }
        return false;
    }
    if (stdoutText) {
        *stdoutText = QString::fromUtf8(process.readAllStandardOutput());
    }
    if (stderrText) {
        *stderrText = QString::fromUtf8(process.readAllStandardError());
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool IpodBackend::discoverDevice(QString *error) {
    QString output;
    QString stderrText;
    if (!run(QStringLiteral("idevice_id"), {QStringLiteral("-l")},
             &output, &stderrText)) {
        if (error) {
            *error = QStringLiteral("Could not query Apple devices. Is libimobiledevice installed?\n%1")
                         .arg(stderrText.trimmed());
        }
        return false;
    }

    const QStringList ids = output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    if (ids.isEmpty()) {
        if (error) {
            *error = QStringLiteral("No paired iPod/iPhone was found. Plug in and unlock the iPod.");
        }
        return false;
    }

    m_udid = ids.first().trimmed();
    QString name;
    run(QStringLiteral("ideviceinfo"),
        {QStringLiteral("-u"), m_udid, QStringLiteral("-k"), QStringLiteral("DeviceName")},
        &name, nullptr, 10000);
    m_deviceName = name.trimmed();
    if (m_deviceName.isEmpty()) {
        m_deviceName = QStringLiteral("iPod");
    }
    return true;
}

bool IpodBackend::isMounted() const {
    QProcess process;
    process.start(QStringLiteral("mountpoint"), {QStringLiteral("-q"), m_mountPoint});
    if (!process.waitForFinished(3000)) {
        return false;
    }
    return process.exitCode() == 0;
}

bool IpodBackend::verifyDeviceMetadata(QString *error) const {
    const QString control = QDir(m_mountPoint).filePath(QStringLiteral("iTunes_Control"));
    const QString hashInfo = QDir(control).filePath(QStringLiteral("Device/HashInfo"));
    const QString sysInfo = QDir(control).filePath(QStringLiteral("Device/SysInfoExtended"));

    if (!QDir(control).exists()) {
        if (error) {
            *error = QStringLiteral("The mounted device does not contain iTunes_Control.");
        }
        return false;
    }
    if (!QFileInfo::exists(sysInfo)) {
        if (error) {
            *error = QStringLiteral("SysInfoExtended is missing. Refusing to modify the iPod.");
        }
        return false;
    }
    if (!QFileInfo::exists(hashInfo)) {
        if (error) {
            *error = QStringLiteral("HashInfo is missing. Refusing to modify the iPod database.");
        }
        return false;
    }
    return true;
}

bool IpodBackend::ensureMounted(QString *error) {
    if (isMounted()) {
        if (m_deviceName.isEmpty()) {
            QString ignored;
            discoverDevice(&ignored);
        }
        return verifyDeviceMetadata(error);
    }

    if (m_udid.isEmpty() && !discoverDevice(error)) {
        return false;
    }

    run(QStringLiteral("gio"),
        {QStringLiteral("mount"), QStringLiteral("-u"), QStringLiteral("afc://%1/").arg(m_udid)},
        nullptr, nullptr, 5000);

    QDir().mkpath(m_mountPoint);
    QString stderrText;
    if (!run(QStringLiteral("ifuse"), {m_mountPoint}, nullptr, &stderrText, 15000)) {
        if (error) {
            *error = QStringLiteral("Could not mount %1 with ifuse.\n%2")
                         .arg(m_deviceName, stderrText.trimmed());
        }
        return false;
    }
    m_ownsMount = true;

    if (!verifyDeviceMetadata(error)) {
        QString ignored;
        unmount(&ignored, true);
        return false;
    }
    return true;
}

bool IpodBackend::unmount(QString *error, bool forceEvenIfNotOwned) {
    if (!isMounted()) {
        m_ownsMount = false;
        return true;
    }
    if (!m_ownsMount && !forceEvenIfNotOwned) {
        return true;
    }

    run(QStringLiteral("sync"), {}, nullptr, nullptr, 30000);
    QString stderrText;
    if (!run(QStringLiteral("fusermount3"), {QStringLiteral("-u"), m_mountPoint},
             nullptr, &stderrText, 15000)) {
        if (error) {
            *error = QStringLiteral("Could not unmount the iPod.\n%1").arg(stderrText.trimmed());
        }
        return false;
    }
    m_ownsMount = false;
    return true;
}

QString IpodBackend::backupDatabase(QString *error) const {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString backupDir = QDir(appData).filePath(QStringLiteral("backups"));
    QDir().mkpath(backupDir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString backup = QDir(backupDir).filePath(QStringLiteral("ipod-db-%1.tar.gz").arg(stamp));

    QString stderrText;
    if (!run(QStringLiteral("tar"),
             {QStringLiteral("-C"), m_mountPoint, QStringLiteral("-czf"), backup,
              QStringLiteral("iTunes_Control/iTunes"), QStringLiteral("iTunes_Control/Device")},
             nullptr, &stderrText, 60000)) {
        if (error) {
            *error = QStringLiteral("Could not back up the iPod database.\n%1")
                         .arg(stderrText.trimmed());
        }
        return {};
    }
    return backup;
}

QVector<TrackInfo> IpodBackend::loadTracks(QString *error) {
    if (!ensureMounted(error)) {
        return {};
    }

    QByteArray output;
    QByteArray errors;
    if (!runHelper({QStringLiteral("list"), m_mountPoint}, &output, &errors, {}, 120000)) {
        if (error) {
            *error = errorFromHelper(errors, QStringLiteral("Could not read the iPod library."));
        }
        return {};
    }

    QVector<TrackInfo> result;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("T\t")) {
            continue;
        }
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 8) {
            continue;
        }
        TrackInfo info;
        info.dbid = fields[1].toULongLong();
        info.title = decode64(fields[2]);
        info.artist = decode64(fields[3]);
        info.album = decode64(fields[4]);
        info.trackNumber = fields[5].toInt();
        info.durationMs = fields[6].toInt();
        info.sizeBytes = fields[7].toULongLong();
        result.push_back(std::move(info));
    }
    return result;
}

AddResult IpodBackend::addTracks(const QStringList &files, const ProgressCallback &progress) {
    AddResult result;
    QString error;
    if (!ensureMounted(&error)) {
        result.errors << error;
        return result;
    }

    result.backupPath = backupDatabase(&error);
    if (result.backupPath.isEmpty()) {
        result.errors << error;
        return result;
    }

    QStringList args{QStringLiteral("add"), m_mountPoint};
    args.append(files);
    QByteArray output;
    QByteArray errors;
    if (!runHelper(args, &output, &errors, progress, 1800000)) {
        result.errors << errorFromHelper(errors, QStringLiteral("The iPod helper failed while adding songs."));
        return result;
    }

    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.split('\t');
        if (line.startsWith("R\t") && fields.size() >= 3) {
            result.added = fields[1].toInt();
            result.skippedDuplicates = fields[2].toInt();
        } else if (line.startsWith("W\t") && fields.size() >= 2) {
            result.errors << decode64(fields[1]);
        }
    }
    run(QStringLiteral("sync"), {}, nullptr, nullptr, 30000);
    return result;
}

RemoveResult IpodBackend::removeTracks(const QList<quint64> &dbids, const ProgressCallback &progress) {
    RemoveResult result;
    QString error;
    if (!ensureMounted(&error)) {
        result.errors << error;
        return result;
    }

    result.backupPath = backupDatabase(&error);
    if (result.backupPath.isEmpty()) {
        result.errors << error;
        return result;
    }

    QStringList args{QStringLiteral("remove"), m_mountPoint};
    for (quint64 id : dbids) {
        args << QString::number(id);
    }

    QByteArray output;
    QByteArray errors;
    if (!runHelper(args, &output, &errors, progress, 1800000)) {
        result.errors << errorFromHelper(errors, QStringLiteral("The iPod helper failed while removing songs."));
        return result;
    }

    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.split('\t');
        if (line.startsWith("R\t") && fields.size() >= 2) {
            result.removed = fields[1].toInt();
        } else if (line.startsWith("W\t") && fields.size() >= 2) {
            result.errors << decode64(fields[1]);
        }
    }
    run(QStringLiteral("sync"), {}, nullptr, nullptr, 30000);
    return result;
}

quint64 IpodBackend::bytesAvailable() const {
    if (!isMounted()) {
        return 0;
    }
    QStorageInfo storage(m_mountPoint);
    return storage.isValid() ? static_cast<quint64>(storage.bytesAvailable()) : 0;
}

quint64 IpodBackend::bytesTotal() const {
    if (!isMounted()) {
        return 0;
    }
    QStorageInfo storage(m_mountPoint);
    return storage.isValid() ? static_cast<quint64>(storage.bytesTotal()) : 0;
}
