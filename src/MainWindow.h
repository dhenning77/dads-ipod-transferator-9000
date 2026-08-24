#pragma once

#include "IpodBackend.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QDropEvent;
class QDragEnterEvent;
class QCloseEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshLibrary();
    void addSongs();
    void addFolder();
    void removeSelected();
    void ejectIpod();
    void filterRows(const QString &text);

private:
    struct LibraryLoadResult {
        QVector<TrackInfo> tracks;
        QString error;
        QString deviceName;
        quint64 bytesAvailable = 0;
        quint64 bytesTotal = 0;
    };

    QStringList collectMp3s(const QStringList &paths) const;
    void addPaths(const QStringList &paths);
    void setBusy(bool busy, const QString &message = {}, bool indeterminate = false);
    void updateProgress(int current, int total, const QString &message);
    void populateLibrary(const LibraryLoadResult &result);
    void updateStatus(const LibraryLoadResult &result);
    static QString humanBytes(quint64 bytes);
    static QString durationText(int durationMs);

    IpodBackend m_backend;
    bool m_busy = false;
    QLabel *m_deviceLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_addSongs = nullptr;
    QPushButton *m_addFolder = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_eject = nullptr;
};
