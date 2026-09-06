# Dad's iPod Transferator 9000 v1.4

A small Qt 6/KDE-friendly music manager for the iPod touch 2G / iOS 4.2.1.

## v1.4 updates

The app now checks GitHub for updates shortly after startup. Update metadata is
resolved from one exact commit on `main`, and the installer downloads and installs
that same snapshot. If a newer version is available, the app asks before installing
it and can restart into the new build when installation completes.

The updater uses the repository `VERSION` file for semantic version comparison and
runs the existing `install.sh` from the downloaded source tree. GitHub/network
failures during the automatic startup check are intentionally silent so they never
interfere with normal iPod use.

Existing v1.3 installations need one normal/manual install of v1.4 to gain the
self-update capability. After that, future versions can update through the app.

## Architecture

The GUI does not link `libgpod`, GLib, or TagLib. Those libraries live in a
short-lived helper process (`dads-ipod-transferator-9000-helper`). This is
intentional: the old libgpod stack works correctly with this iPod from its
standalone command-line tools, while embedding it into the Qt process produced
allocator corruption on current CachyOS.

If the helper ever fails, the GUI survives and reports the error. The OS
reclaims the helper heap at process exit, so we also avoid the problematic
libgpod teardown path.

## Features

- Automatic iPod discovery and ifuse mount
- List/search library
- Add individual MP3s
- Add folders recursively
- Drag-and-drop MP3s/folders
- Duplicate skipping
- Multi-select removal
- Database backup before each mutation
- Responsive UI with transfer progress
- Safe eject
- GitHub update check and in-app update installation

## Build/install

```bash
./install.sh
```

The installer writes:

- `~/.local/bin/dads-ipod-transferator-9000`
- `~/.local/bin/dads-ipod-transferator-9000-helper`
- KDE desktop entry under `~/.local/share/applications`

Keep the locally rebuilt working `libgpod` package already installed on this
CachyOS system.
