#include "MainWindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QtConcurrent>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Dad's iPod Transferator 9000"));
    resize(980, 650);
    setAcceptDrops(true);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(10);

    auto *heading = new QLabel(QStringLiteral("<b>Dad's iPod Transferator 9000</b>"), central);
    QFont headingFont = heading->font();
    headingFont.setPointSize(16);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    m_deviceLabel = new QLabel(QStringLiteral("Looking for iPod…"), central);
    layout->addWidget(m_deviceLabel);

    auto *buttons = new QHBoxLayout();
    m_addSongs = new QPushButton(QStringLiteral("Add Songs…"), central);
    m_addFolder = new QPushButton(QStringLiteral("Add Folder…"), central);
    m_remove = new QPushButton(QStringLiteral("Remove Selected"), central);
    m_refresh = new QPushButton(QStringLiteral("Refresh"), central);
    m_eject = new QPushButton(QStringLiteral("Eject"), central);
    buttons->addWidget(m_addSongs);
    buttons->addWidget(m_addFolder);
    buttons->addWidget(m_remove);
    buttons->addStretch();
    buttons->addWidget(m_refresh);
    buttons->addWidget(m_eject);
    layout->addLayout(buttons);

    m_search = new QLineEdit(central);
    m_search->setPlaceholderText(QStringLiteral("Search title, artist, or album…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_table = new QTableWidget(0, 5, central);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Title"), QStringLiteral("Artist"), QStringLiteral("Album"),
        QStringLiteral("Time"), QStringLiteral("Size")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    m_progress = new QProgressBar(central);
    m_progress->setTextVisible(true);
    m_progress->setMinimumHeight(18);
    m_progress->hide();
    layout->addWidget(m_progress);

    m_summaryLabel = new QLabel(QStringLiteral("Drop MP3s or folders anywhere in this window to add them."), central);
    layout->addWidget(m_summaryLabel);

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Ready"));

    connect(m_refresh, &QPushButton::clicked, this, &MainWindow::refreshLibrary);
    connect(m_addSongs, &QPushButton::clicked, this, &MainWindow::addSongs);
    connect(m_addFolder, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(m_remove, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(m_eject, &QPushButton::clicked, this, &MainWindow::ejectIpod);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::filterRows);

    QTimer::singleShot(0, this, &MainWindow::refreshLibrary);
}

void MainWindow::setBusy(bool busy, const QString &message, bool indeterminate) {
    m_busy = busy;
    m_addSongs->setEnabled(!busy);
    m_addFolder->setEnabled(!busy);
    m_remove->setEnabled(!busy);
    m_refresh->setEnabled(!busy);
    m_eject->setEnabled(!busy);

    if (busy) {
        if (indeterminate) {
            m_progress->setRange(0, 0);
            m_progress->setFormat(QString());
        }
        m_progress->show();
        if (!QApplication::overrideCursor()) {
            QApplication::setOverrideCursor(QCursor(Qt::BusyCursor));
        }
    } else {
        m_progress->hide();
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        m_progress->setFormat(QStringLiteral("%p%"));
        if (QApplication::overrideCursor()) {
            QApplication::restoreOverrideCursor();
        }
    }

    if (!message.isEmpty()) {
        statusBar()->showMessage(message);
    }
}

void MainWindow::updateProgress(int current, int total, const QString &message) {
    if (!m_busy) {
        return;
    }
    if (total > 0) {
        m_progress->setRange(0, total);
        m_progress->setValue(std::clamp(current, 0, total));
        m_progress->setFormat(QStringLiteral("%v / %m"));
    } else {
        m_progress->setRange(0, 0);
        m_progress->setFormat(QString());
    }
    if (!message.isEmpty()) {
        statusBar()->showMessage(message);
    }
}

QString MainWindow::durationText(int durationMs) {
    const int seconds = std::max(0, durationMs / 1000);
    return QStringLiteral("%1:%2")
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString MainWindow::humanBytes(quint64 bytes) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    const int precision = unit <= 1 ? 0 : 1;
    return QStringLiteral("%1 %2").arg(value, 0, 'f', precision).arg(QString::fromLatin1(units[unit]));
}

void MainWindow::updateStatus(const LibraryLoadResult &result) {
    m_deviceLabel->setText(QStringLiteral("<b>%1</b> connected").arg(result.deviceName.toHtmlEscaped()));
    if (result.bytesTotal > 0) {
        m_summaryLabel->setText(QStringLiteral("%1 songs • %2 free of %3")
                                    .arg(result.tracks.size())
                                    .arg(humanBytes(result.bytesAvailable))
                                    .arg(humanBytes(result.bytesTotal)));
    } else {
        m_summaryLabel->setText(QStringLiteral("%1 songs").arg(result.tracks.size()));
    }
}

void MainWindow::populateLibrary(const LibraryLoadResult &result) {
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    for (const TrackInfo &track : result.tracks) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto *title = new QTableWidgetItem(track.title);
        title->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(track.dbid));
        auto *artist = new QTableWidgetItem(track.artist);
        auto *album = new QTableWidgetItem(track.album);
        auto *duration = new QTableWidgetItem(durationText(track.durationMs));
        auto *size = new QTableWidgetItem(humanBytes(track.sizeBytes));
        size->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(track.sizeBytes));

        m_table->setItem(row, 0, title);
        m_table->setItem(row, 1, artist);
        m_table->setItem(row, 2, album);
        m_table->setItem(row, 3, duration);
        m_table->setItem(row, 4, size);
    }
    m_table->setSortingEnabled(true);
    m_table->sortItems(1, Qt::AscendingOrder);
    filterRows(m_search->text());
    updateStatus(result);
}

void MainWindow::refreshLibrary() {
    if (m_busy) {
        return;
    }

    setBusy(true, QStringLiteral("Reading iPod library…"), true);

    auto *watcher = new QFutureWatcher<LibraryLoadResult>(this);
    connect(watcher, &QFutureWatcher<LibraryLoadResult>::finished, this, [this, watcher]() {
        const LibraryLoadResult result = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        if (!result.error.isEmpty()) {
            m_deviceLabel->setText(QStringLiteral("iPod not connected"));
            m_table->setRowCount(0);
            m_summaryLabel->setText(QStringLiteral("Plug in and unlock the iPod, then click Refresh."));
            statusBar()->showMessage(QStringLiteral("iPod not ready"));
            QMessageBox::warning(this, QStringLiteral("Dad's iPod Transferator 9000"), result.error);
            return;
        }

        populateLibrary(result);
        statusBar()->showMessage(QStringLiteral("Ready"));
    });

    watcher->setFuture(QtConcurrent::run([this]() {
        LibraryLoadResult result;
        result.tracks = m_backend.loadTracks(&result.error);
        if (result.error.isEmpty()) {
            result.deviceName = m_backend.deviceName();
            result.bytesAvailable = m_backend.bytesAvailable();
            result.bytesTotal = m_backend.bytesTotal();
        }
        return result;
    }));
}

QStringList MainWindow::collectMp3s(const QStringList &paths) const {
    QStringList files;
    QSet<QString> seen;

    auto addFile = [&](const QString &path) {
        QFileInfo info(path);
        if (!info.exists() || !info.isFile() || !path.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive)) {
            return;
        }
        const QString canonical = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
        if (!seen.contains(canonical)) {
            seen.insert(canonical);
            files << canonical;
        }
    };

    for (const QString &path : paths) {
        QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(info.absoluteFilePath(), {QStringLiteral("*.mp3"), QStringLiteral("*.MP3")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                addFile(it.next());
            }
        } else {
            addFile(path);
        }
    }

    files.sort(Qt::CaseInsensitive);
    return files;
}

void MainWindow::addPaths(const QStringList &paths) {
    if (m_busy) {
        return;
    }

    const QStringList files = collectMp3s(paths);
    if (files.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Dad's iPod Transferator 9000"),
                                 QStringLiteral("No MP3 files were found."));
        return;
    }

    setBusy(true, QStringLiteral("Preparing to add %1 song(s)…").arg(files.size()));
    m_progress->setRange(0, files.size() + 1);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("%v / %m"));

    auto *watcher = new QFutureWatcher<AddResult>(this);
    connect(watcher, &QFutureWatcher<AddResult>::finished, this, [this, watcher]() {
        const AddResult result = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        QString message = QStringLiteral("Added %1 song(s).").arg(result.added);
        if (result.skippedDuplicates > 0) {
            message += QStringLiteral("\nSkipped %1 duplicate(s).").arg(result.skippedDuplicates);
        }
        if (!result.backupPath.isEmpty()) {
            message += QStringLiteral("\n\nBackup: %1").arg(result.backupPath);
        }
        if (!result.errors.isEmpty()) {
            message += QStringLiteral("\n\nProblems:\n• ") + result.errors.join(QStringLiteral("\n• "));
            QMessageBox::warning(this, QStringLiteral("Transfer finished with warnings"), message);
        } else {
            QMessageBox::information(this, QStringLiteral("Transfer complete"), message);
        }
        refreshLibrary();
    });

    watcher->setFuture(QtConcurrent::run([this, files]() {
        return m_backend.addTracks(files, [this](int current, int total, const QString &message) {
            QMetaObject::invokeMethod(this, [this, current, total, message]() {
                updateProgress(current, total, message);
            }, Qt::QueuedConnection);
        });
    }));
}

void MainWindow::addSongs() {
    if (m_busy) {
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add songs to iPod"), QDir::homePath(),
        QStringLiteral("MP3 audio (*.mp3)"));
    if (!files.isEmpty()) {
        addPaths(files);
    }
}

void MainWindow::addFolder() {
    if (m_busy) {
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Add folder to iPod"), QDir::homePath());
    if (!folder.isEmpty()) {
        addPaths({folder});
    }
}

void MainWindow::removeSelected() {
    if (m_busy) {
        return;
    }

    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Dad's iPod Transferator 9000"),
                                 QStringLiteral("Select one or more songs first."));
        return;
    }

    QList<quint64> ids;
    QStringList names;
    for (const QModelIndex &index : selected) {
        QTableWidgetItem *titleItem = m_table->item(index.row(), 0);
        QTableWidgetItem *artistItem = m_table->item(index.row(), 1);
        if (!titleItem) {
            continue;
        }
        ids << titleItem->data(Qt::UserRole).toULongLong();
        names << QStringLiteral("%1 — %2")
                     .arg(artistItem ? artistItem->text() : QString(), titleItem->text());
    }

    const QString preview = names.mid(0, 8).join(QStringLiteral("\n"));
    const QString more = names.size() > 8
        ? QStringLiteral("\n…and %1 more").arg(names.size() - 8)
        : QString();
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Remove songs from iPod?"),
        QStringLiteral("Remove %1 song(s) from the iPod?\n\n%2%3\n\nA database backup will be created first.")
            .arg(ids.size()).arg(preview).arg(more),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }

    setBusy(true, QStringLiteral("Preparing to remove %1 song(s)…").arg(ids.size()));
    m_progress->setRange(0, ids.size() + 1);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("%v / %m"));

    auto *watcher = new QFutureWatcher<RemoveResult>(this);
    connect(watcher, &QFutureWatcher<RemoveResult>::finished, this, [this, watcher]() {
        const RemoveResult result = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        QString message = QStringLiteral("Removed %1 song(s).").arg(result.removed);
        if (!result.backupPath.isEmpty()) {
            message += QStringLiteral("\n\nBackup: %1").arg(result.backupPath);
        }
        if (!result.errors.isEmpty()) {
            message += QStringLiteral("\n\nProblems:\n• ") + result.errors.join(QStringLiteral("\n• "));
            QMessageBox::warning(this, QStringLiteral("Removal finished with warnings"), message);
        } else {
            QMessageBox::information(this, QStringLiteral("Removal complete"), message);
        }
        refreshLibrary();
    });

    watcher->setFuture(QtConcurrent::run([this, ids]() {
        return m_backend.removeTracks(ids, [this](int current, int total, const QString &message) {
            QMetaObject::invokeMethod(this, [this, current, total, message]() {
                updateProgress(current, total, message);
            }, Qt::QueuedConnection);
        });
    }));
}

void MainWindow::ejectIpod() {
    if (m_busy) {
        return;
    }

    setBusy(true, QStringLiteral("Ejecting iPod…"), true);
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        const QString error = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Could not eject"), error);
            return;
        }
        m_table->setRowCount(0);
        m_deviceLabel->setText(QStringLiteral("iPod ejected"));
        m_summaryLabel->setText(QStringLiteral("It is safe to unplug the iPod."));
        statusBar()->showMessage(QStringLiteral("Ejected"));
    });

    watcher->setFuture(QtConcurrent::run([this]() {
        QString error;
        if (!m_backend.unmount(&error, true)) {
            return error;
        }
        return QString();
    }));
}

void MainWindow::filterRows(const QString &text) {
    const QString needle = text.trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool match = needle.isEmpty();
        for (int col = 0; col < 3 && !match; ++col) {
            QTableWidgetItem *item = m_table->item(row, col);
            match = item && item->text().contains(needle, Qt::CaseInsensitive);
        }
        m_table->setRowHidden(row, !match);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (!m_busy && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    if (m_busy) {
        event->ignore();
        return;
    }

    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    if (!paths.isEmpty()) {
        addPaths(paths);
        event->acceptProposedAction();
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_busy) {
        statusBar()->showMessage(QStringLiteral("Finish the current iPod operation before closing."), 5000);
        event->ignore();
        return;
    }

    QString ignored;
    m_backend.unmount(&ignored, false);
    QMainWindow::closeEvent(event);
}
