#include <gpod/itdb.h>
#include <glib.h>

#include <audioproperties.h>
#include <mpegfile.h>
#include <tag.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string safe(const gchar *s) {
    return s ? std::string(s) : std::string();
}

std::string tagString(const TagLib::String &s) {
    return std::string(s.toCString(true));
}

std::string b64(const std::string &s) {
    gchar *encoded = g_base64_encode(reinterpret_cast<const guchar *>(s.data()), s.size());
    std::string out = encoded ? encoded : "";
    g_free(encoded);
    return out;
}

void emitError(const std::string &message) {
    std::fprintf(stderr, "ERROR\t%s\n", b64(message).c_str());
    std::fflush(stderr);
}

void emitWarning(const std::string &message) {
    std::printf("W\t%s\n", b64(message).c_str());
    std::fflush(stdout);
}

void emitProgress(int current, int total, const std::string &message) {
    std::printf("P\t%d\t%d\t%s\n", current, total, b64(message).c_str());
    std::fflush(stdout);
}

std::string lowerTrimmed(const std::string &value) {
    gchar *folded = g_utf8_casefold(value.c_str(), -1);
    gchar *normalized = folded ? g_utf8_normalize(folded, -1, G_NORMALIZE_ALL_COMPOSE) : nullptr;
    std::string out = normalized ? normalized : (folded ? folded : value);
    g_free(normalized);
    g_free(folded);
    return out;
}

std::string fingerprint(const std::string &artist, const std::string &album,
                        const std::string &title, int trackNumber) {
    return lowerTrimmed(artist) + "\x1f" + lowerTrimmed(album) + "\x1f" +
           lowerTrimmed(title) + "\x1f" + std::to_string(trackNumber);
}

std::string fingerprint(const Itdb_Track *track) {
    return fingerprint(safe(track->artist), safe(track->album), safe(track->title), track->track_nr);
}

Itdb_iTunesDB *parseDb(const std::string &mountPoint, std::string &error) {
    GError *gerror = nullptr;
    Itdb_iTunesDB *db = itdb_parse(mountPoint.c_str(), &gerror);
    if (!db) {
        error = (gerror && gerror->message) ? gerror->message : "libgpod could not parse the iPod database.";
    }
    if (gerror) {
        g_error_free(gerror);
    }
    return db;
}

Itdb_Track *trackFromMp3(const std::string &path, std::string &error) {
    TagLib::MPEG::File file(path.c_str());
    if (!file.isValid()) {
        error = "Could not open MP3: " + path;
        return nullptr;
    }

    TagLib::Tag *tag = file.tag();
    if (!tag) {
        error = "Could not read MP3 tags: " + path;
        return nullptr;
    }

    TagLib::AudioProperties *audio = file.audioProperties();
    std::string title = tagString(tag->title());
    if (title.empty()) {
        title = fs::path(path).stem().string();
    }
    const std::string artist = tagString(tag->artist());
    const std::string album = tagString(tag->album());

    Itdb_Track *track = itdb_track_new();
    track->title = g_strdup(title.c_str());
    track->album = g_strdup(album.c_str());
    track->artist = g_strdup(artist.c_str());
    track->albumartist = g_strdup(artist.c_str());
    track->genre = g_strdup(tagString(tag->genre()).c_str());
    track->comment = g_strdup(tagString(tag->comment()).c_str());
    track->filetype = g_strdup("MP3-file");
    track->track_nr = static_cast<gint32>(tag->track());
    track->year = static_cast<gint32>(tag->year());
    track->mediatype = 0x01;
    track->time_added = std::time(nullptr);

    std::error_code ec;
    const auto fileSize = fs::file_size(path, ec);
    track->size = ec ? 0 : static_cast<gint32>(std::min<std::uintmax_t>(
        fileSize, static_cast<std::uintmax_t>(std::numeric_limits<gint32>::max())));

    const auto modified = fs::last_write_time(path, ec);
    if (!ec) {
        const auto systemNow = std::chrono::system_clock::now();
        const auto fileNow = fs::file_time_type::clock::now();
        const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            modified - fileNow + systemNow);
        track->time_modified = std::chrono::system_clock::to_time_t(systemTime);
    }

    if (audio) {
        track->tracklen = audio->length() * 1000;
        track->bitrate = audio->bitrate();
        track->samplerate = static_cast<guint16>(std::clamp(audio->sampleRate(), 0, 65535));
    }

    return track;
}

Itdb_Track *findTrackByDbid(Itdb_iTunesDB *db, guint64 dbid) {
    for (GList *node = db->tracks; node; node = node->next) {
        auto *track = static_cast<Itdb_Track *>(node->data);
        if (track && track->dbid == dbid) {
            return track;
        }
    }
    return nullptr;
}

int listTracks(const std::string &mountPoint) {
    std::string error;
    Itdb_iTunesDB *db = parseDb(mountPoint, error);
    if (!db) {
        emitError(error);
        return 2;
    }

    Itdb_Playlist *master = itdb_playlist_mpl(db);
    if (!master) {
        emitError("The iPod database does not contain a master music playlist.");
        return 3;
    }

    for (GList *node = master->members; node; node = node->next) {
        auto *track = static_cast<Itdb_Track *>(node->data);
        if (!track) {
            continue;
        }
        std::printf("T\t%llu\t%s\t%s\t%s\t%d\t%d\t%u\n",
                    static_cast<unsigned long long>(track->dbid),
                    b64(safe(track->title)).c_str(),
                    b64(safe(track->artist)).c_str(),
                    b64(safe(track->album)).c_str(),
                    track->track_nr,
                    track->tracklen,
                    static_cast<unsigned int>(std::max<gint32>(0, track->size)));
    }
    std::fflush(stdout);

    // Intentionally do not call itdb_free() in this short-lived helper.
    // The standalone libgpod parser is reliable on this old iPod, but the Qt
    // process repeatedly hit allocator corruption during libgpod teardown.
    // Process isolation lets the OS reclaim the complete heap safely.
    return 0;
}

int addTracks(const std::string &mountPoint, const std::vector<std::string> &files) {
    std::string error;
    Itdb_iTunesDB *db = parseDb(mountPoint, error);
    if (!db) {
        emitError(error);
        return 2;
    }
    Itdb_Playlist *master = itdb_playlist_mpl(db);
    if (!master) {
        emitError("No master music playlist exists on the iPod.");
        return 3;
    }

    std::set<std::string> existing;
    for (GList *node = db->tracks; node; node = node->next) {
        auto *track = static_cast<Itdb_Track *>(node->data);
        if (track) {
            existing.insert(fingerprint(track));
        }
    }

    int added = 0;
    int skipped = 0;
    const int total = static_cast<int>(files.size()) + 1;

    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::string &path = files[i];
        emitProgress(static_cast<int>(i), total,
                     "Adding " + fs::path(path).filename().string() +
                     " (" + std::to_string(i + 1) + "/" + std::to_string(files.size()) + ")...");

        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) {
            emitWarning("File not found: " + path);
            continue;
        }

        std::string trackError;
        Itdb_Track *track = trackFromMp3(path, trackError);
        if (!track) {
            emitWarning(trackError);
            continue;
        }

        const std::string fp = fingerprint(track);
        if (existing.find(fp) != existing.end()) {
            ++skipped;
            itdb_track_free(track);
            continue;
        }

        itdb_track_add(db, track, -1);
        itdb_playlist_add_track(master, track, -1);

        GError *gerror = nullptr;
        if (!itdb_cp_track_to_ipod(track, path.c_str(), &gerror)) {
            const std::string message = (gerror && gerror->message)
                ? gerror->message
                : "libgpod failed to copy " + path;
            if (gerror) {
                g_error_free(gerror);
            }
            itdb_playlist_remove_track(master, track);
            itdb_track_remove(track);
            emitWarning(message);
            continue;
        }

        existing.insert(fp);
        ++added;
    }

    if (added > 0) {
        emitProgress(static_cast<int>(files.size()), total, "Updating iPod library...");
        GError *writeError = nullptr;
        if (!itdb_write(db, &writeError)) {
            const std::string message = (writeError && writeError->message)
                ? writeError->message
                : "libgpod failed to write the updated iPod database.";
            if (writeError) {
                g_error_free(writeError);
            }
            emitError(message);
            return 4;
        }
        if (writeError) {
            g_error_free(writeError);
        }
    }

    emitProgress(total, total, added > 0 ? "Transfer complete." : "Nothing new to add.");
    std::printf("R\t%d\t%d\n", added, skipped);
    std::fflush(stdout);
    return 0;
}

int removeTracks(const std::string &mountPoint, const std::vector<guint64> &dbids) {
    std::string error;
    Itdb_iTunesDB *db = parseDb(mountPoint, error);
    if (!db) {
        emitError(error);
        return 2;
    }

    std::vector<std::string> filesToDelete;
    int removed = 0;
    const int total = static_cast<int>(dbids.size()) + 1;

    for (std::size_t i = 0; i < dbids.size(); ++i) {
        emitProgress(static_cast<int>(i), total,
                     "Removing song " + std::to_string(i + 1) + " of " + std::to_string(dbids.size()) + "...");

        Itdb_Track *track = findTrackByDbid(db, dbids[i]);
        if (!track) {
            emitWarning("Could not find track id " + std::to_string(dbids[i]) + " in the current database.");
            continue;
        }

        gchar *physical = itdb_filename_on_ipod(track);
        if (physical) {
            filesToDelete.emplace_back(physical);
            g_free(physical);
        }

        for (GList *plNode = db->playlists; plNode; plNode = plNode->next) {
            auto *playlist = static_cast<Itdb_Playlist *>(plNode->data);
            if (!playlist) {
                continue;
            }
            while (itdb_playlist_contains_track(playlist, track)) {
                itdb_playlist_remove_track(playlist, track);
            }
        }
        itdb_track_remove(track);
        ++removed;
    }

    if (removed > 0) {
        emitProgress(static_cast<int>(dbids.size()), total, "Updating iPod library...");
        GError *writeError = nullptr;
        if (!itdb_write(db, &writeError)) {
            const std::string message = (writeError && writeError->message)
                ? writeError->message
                : "libgpod failed to write the updated iPod database. No music files were deleted.";
            if (writeError) {
                g_error_free(writeError);
            }
            emitError(message);
            return 4;
        }
        if (writeError) {
            g_error_free(writeError);
        }

        for (const std::string &path : filesToDelete) {
            std::error_code ec;
            if (fs::exists(path, ec) && !fs::remove(path, ec)) {
                emitWarning("Removed from the library but could not delete file: " + path);
            }
        }
    }

    emitProgress(total, total, removed > 0 ? "Removal complete." : "Nothing to remove.");
    std::printf("R\t%d\n", removed);
    std::fflush(stdout);
    return 0;
}

void usage(const char *argv0) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s list <mountpoint>\n"
        "  %s add <mountpoint> <mp3>...\n"
        "  %s remove <mountpoint> <dbid>...\n",
        argv0, argv0, argv0);
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 64;
    }

    const std::string command = argv[1];
    const std::string mountPoint = argv[2];

    if (command == "list") {
        return listTracks(mountPoint);
    }
    if (command == "add") {
        if (argc < 4) {
            emitError("No MP3 files were provided.");
            return 64;
        }
        std::vector<std::string> files;
        for (int i = 3; i < argc; ++i) {
            files.emplace_back(argv[i]);
        }
        return addTracks(mountPoint, files);
    }
    if (command == "remove") {
        if (argc < 4) {
            emitError("No track ids were provided.");
            return 64;
        }
        std::vector<guint64> ids;
        for (int i = 3; i < argc; ++i) {
            char *end = nullptr;
            errno = 0;
            const unsigned long long value = std::strtoull(argv[i], &end, 10);
            if (errno != 0 || !end || *end != '\0') {
                emitError(std::string("Invalid track id: ") + argv[i]);
                return 64;
            }
            ids.push_back(static_cast<guint64>(value));
        }
        return removeTracks(mountPoint, ids);
    }

    usage(argv[0]);
    return 64;
}
