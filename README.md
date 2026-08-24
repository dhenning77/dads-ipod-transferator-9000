# Dad's iPod Transferator 9000 v1.3

A small Qt 6/KDE-friendly music manager for the iPod touch 2G / iOS 4.2.1.

## v1.3 architecture

The GUI no longer links `libgpod`, GLib, or TagLib. Those libraries live in a
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
