#include "PlayerApp.hpp"

#include <firmir/gui/image.hpp>
#include <firmir/gui/listmodel.hpp>
#include <firmir/multimedia/videooutput.hpp>
#include <firmir/multimedia/videosource.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {

const char* const kAudioExt[] = {".mp3", ".flac", ".ogg",
                                 ".wav", ".m4a", ".aac", ".opus"};
const char* const kVideoExt[] = {".mp4", ".mkv", ".webm", ".avi", ".mov"};
const char* const kImageExt[] = {".png", ".jpg", ".jpeg", ".bmp"};

bool hasExtension(const std::filesystem::path& path, const char* const* list,
                  std::size_t count) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (std::size_t i = 0; i < count; ++i)
        if (ext == list[i])
            return true;
    return false;
}

bool isVideoFile(const std::filesystem::path& path) {
    return hasExtension(path, kVideoExt, std::size(kVideoExt));
}
bool isAudioFile(const std::filesystem::path& path) {
    return hasExtension(path, kAudioExt, std::size(kAudioExt));
}
bool isImageFile(const std::filesystem::path& path) {
    return hasExtension(path, kImageExt, std::size(kImageExt));
}

} // namespace

PlayerApp::PlayerApp(Object* parent) : Object(parent) {
    setProperty("page", 0.0);
    setProperty("nowPlaying", Firmir::String(""));
    setProperty("positionText", Firmir::String("0:00"));
    setProperty("position", 0.0); // seconds; drives the seek bar fill
    setProperty("duration", 0.0);
    setProperty("statusText", Firmir::String(""));
    setProperty("currentDir", Firmir::String(""));

    // Position/duration readout.
    m_positionTimer.setInterval(std::chrono::milliseconds(500));
    m_positionTimer.connect(this, "timeout",
                            [this] { updatePositionText(); });
    m_positionTimer.start();
}

PlayerApp::~PlayerApp() {
    if (m_source)
        m_source->close();
}

void PlayerApp::attach(const std::string& path,
                       Firmir::Multimedia::VideoOutput* video,
                       Firmir::Gui::Image* photo,
                       Firmir::Gui::ListModel* fileModel) {
    m_video = video;
    m_photo = photo;
    m_model = fileModel;

    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(path, ec);
    const std::string absPath = (ec ? std::filesystem::path(path) : abs).string();

    // The decode backend (plugins/multimedia/), with per-file fallback.
    m_source = Firmir::Multimedia::createVideoSource(Firmir::String("auto"), this);
    if (m_source)
        m_video->setSource(m_source);
    setProperty("statusText",
                Firmir::String(
                    m_source
                        ? (std::string("backend: ") + m_source->backendName()).c_str()
                        : "no decode backend (install gstreamer/ffmpeg dev libs)"));

    if (m_source) {
        // Track end -> next media file in the directory (the backends emit
        // Stopped at EOS).
        m_source->connect<int>(
            this, Firmir::Multimedia::VideoSource::signalStateChanged,
            [this](int state) {
                if (state == static_cast<int>(
                        Firmir::Multimedia::VideoSource::State::Stopped) &&
                    m_current >= 0)
                    stepMedia(+1);
            },
            Firmir::ConnectionMode::Queued);
    }

    if (std::filesystem::is_regular_file(abs, ec)) {
        // A single file argument: browse its directory and open the file.
        m_dir = abs.parent_path().string();
        if (m_dir.empty())
            m_dir = "/";
        refreshDirectory();
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].path == absPath) {
                activate(static_cast<double>(i));
                break;
            }
        }
    } else {
        m_dir = std::filesystem::is_directory(abs, ec) ? absPath : ".";
        refreshDirectory();
    }
}

void PlayerApp::showPage(double page) { setProperty("page", page); }

void PlayerApp::refreshDirectory() {
    m_entries.clear();
    if (m_model)
        m_model->clear();

    std::error_code ec;
    // ".." first unless at the root.
    const std::filesystem::path dir(m_dir);
    if (dir.has_parent_path() && dir != dir.root_path())
        m_entries.push_back(
            Entry{dir.parent_path().string(), "..", Kind::Directory});

    for (const auto& item : std::filesystem::directory_iterator(m_dir, ec)) {
        const std::string name = item.path().filename().string();
        if (name.empty() || name[0] == '.')
            continue; // skip hidden entries
        std::error_code typeEc;
        Entry entry{item.path().string(), name, Kind::Directory};
        if (item.is_directory(typeEc))
            entry.kind = Kind::Directory;
        else if (isAudioFile(item.path()))
            entry.kind = Kind::Audio;
        else if (isVideoFile(item.path()))
            entry.kind = Kind::Video;
        else if (isImageFile(item.path()))
            entry.kind = Kind::ImageFile;
        else
            continue; // not a media file: don't list
        m_entries.push_back(std::move(entry));
    }
    // Directories first, then files; alphabetical within each group.
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [](const Entry& a, const Entry& b) {
                         const bool aDir = a.kind == Kind::Directory;
                         const bool bDir = b.kind == Kind::Directory;
                         if (a.label == ".." || b.label == "..")
                             return a.label == ".." && b.label != "..";
                         if (aDir != bDir)
                             return aDir;
                         return a.label < b.label;
                     });

    if (m_model) {
        for (const Entry& entry : m_entries) {
            const bool dir = entry.kind == Kind::Directory;
            const bool video = entry.kind == Kind::Video;
            const bool image = entry.kind == Kind::ImageFile;
            const std::string label =
                dir ? "[" + entry.label + "]" : entry.label;
            m_model->append({{"label", Firmir::String(label.c_str())},
                             {"dir", dir},
                             {"video", video},
                             {"image", image}});
        }
    }
    setProperty("currentDir", Firmir::String(m_dir.c_str()));
    // The listing changed: entry indices no longer match the playing file.
    m_current = -1;
}

void PlayerApp::activate(double index) {
    const int i = static_cast<int>(index);
    if (i < 0 || i >= static_cast<int>(m_entries.size()))
        return;
    const Entry& entry = m_entries[std::size_t(i)];
    switch (entry.kind) {
    case Kind::Directory:
        m_dir = entry.path;
        refreshDirectory();
        setProperty("page", 0.0); // the browser shows the new directory
        break;
    case Kind::Audio:
    case Kind::Video:
        playFile(i);
        break;
    case Kind::ImageFile:
        showImage(i);
        break;
    }
}

void PlayerApp::goUp() {
    const std::filesystem::path dir(m_dir);
    if (!dir.has_parent_path() || dir == dir.root_path())
        return;
    m_dir = dir.parent_path().string();
    refreshDirectory();
}

void PlayerApp::openPath(Firmir::String path) {
    const std::string typed = path.toStdString();
    if (typed.empty())
        return;
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(typed, ec);
    const std::string absPath = (ec ? std::filesystem::path(typed) : abs).string();
    if (std::filesystem::is_directory(abs, ec)) {
        m_dir = absPath;
        refreshDirectory();
        setProperty("page", 0.0);
        return;
    }
    if (!std::filesystem::is_regular_file(abs, ec)) {
        setProperty("statusText", Firmir::String("no such file or directory"));
        return;
    }
    // Browse the file's directory, then activate the entry (keeps the
    // media/image handling in one place and makes next/prev work).
    m_dir = abs.parent_path().string();
    if (m_dir.empty())
        m_dir = "/";
    refreshDirectory();
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == absPath) {
            activate(static_cast<double>(i));
            return;
        }
    }
    setProperty("statusText", Firmir::String("unsupported file type"));
}

void PlayerApp::play() {
    if (m_source)
        m_source->play();
}
void PlayerApp::pause() {
    if (m_source)
        m_source->pause();
}
void PlayerApp::stop() {
    if (m_source)
        m_source->stop();
}

void PlayerApp::seek(double seconds) {
    if (!m_source)
        return;
    const double duration = m_source->duration();
    if (duration <= 0)
        return; // unknown length (stream) — nowhere to seek to
    m_source->seek(std::max(0.0, std::min(seconds, duration)));
    updatePositionText(); // instant feedback; the timer refreshes the rest
}
void PlayerApp::nextTrack() { stepMedia(+1); }
void PlayerApp::prevTrack() { stepMedia(-1); }

/// Plays the next (direction > 0) or previous (direction < 0) audio/video
/// entry relative to the current one.
void PlayerApp::stepMedia(int direction) {
    if (m_current < 0)
        return;
    int i = m_current;
    for (;;) {
        i += direction;
        if (i < 0 || i >= static_cast<int>(m_entries.size()))
            return; // end of the listing: stop walking
        const Kind kind = m_entries[std::size_t(i)].kind;
        if (kind == Kind::Audio || kind == Kind::Video) {
            playFile(i);
            return;
        }
    }
}

void PlayerApp::showImage(int index) {
    const Entry& entry = m_entries[std::size_t(index)];
    if (m_photo) {
        m_photo->setSource(Firmir::String(entry.path.c_str()));
        setProperty("page", 2.0); // the photos page hosts the Image item
        setProperty("nowPlaying",
                    Firmir::String(entry.label.c_str()));
        setProperty("statusText",
                    m_photo->status() == Firmir::Gui::Image::Status::Error
                        ? Firmir::String("cannot decode image")
                        : Firmir::String(""));
    }
}

void PlayerApp::playFile(int index) {
    if (!m_source)
        return;
    m_current = index;
    const Entry& entry = m_entries[std::size_t(index)];
    const std::string& file = entry.path;
    m_source->stop();
    m_source->close();
    bool opened = m_source->open(Firmir::String(file.c_str()));
    if (!opened) {
        // The chosen backend can't play this file (e.g. gstreamer without
        // the demuxer/decoder plugins for it): try the rest.
        for (const std::string& backend :
             Firmir::Multimedia::availableVideoSourceBackends()) {
            if (backend == m_source->backendName())
                continue;
            auto* next = Firmir::Multimedia::createVideoSource(
                Firmir::String(backend.c_str()), this);
            if (!next)
                continue;
            if (next->open(Firmir::String(file.c_str()))) {
                m_video->setSource(next);
                delete m_source; // a child of this: removed from the tree
                m_source = next;
                setProperty("statusText",
                            Firmir::String(
                                (std::string("backend: ") + backend).c_str()));
                opened = true;
                break;
            }
            delete next;
        }
    }
    if (!opened) {
        setProperty("statusText", Firmir::String("open failed"));
        return;
    }
    // Video files switch to the video page automatically.
    setProperty("page", entry.kind == Kind::Video ? 1.0 : 0.0);
    m_source->play();
    setProperty("nowPlaying", Firmir::String(entry.label.c_str()));
}

void PlayerApp::updatePositionText() {
    if (!m_source)
        return;
    const double pos = m_source->position();
    const double dur = m_source->duration();
    setProperty("position", pos);
    setProperty("duration", dur > 0 ? dur : 0.0);
    char text[48];
    if (dur > 0)
        std::snprintf(text, sizeof(text), "%d:%02d / %d:%02d",
                      static_cast<int>(pos) / 60, static_cast<int>(pos) % 60,
                      static_cast<int>(dur) / 60, static_cast<int>(dur) % 60);
    else
        std::snprintf(text, sizeof(text), "%d:%02d", static_cast<int>(pos) / 60,
                      static_cast<int>(pos) % 60);
    setProperty("positionText", Firmir::String(text));
}
