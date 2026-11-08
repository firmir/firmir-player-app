#pragma once

// firmir-player application logic, exposed to the Firml UI (player.fml).
//
// The app is a small file browser: it lists the current directory
// (directories first), enters directories on click, plays audio/video
// files through the multimedia backends and shows PNG/JPEG images in an
// Image item. Pages switch through the "page" property (0 = browser,
// 1 = video, 2 = photos); the transport buttons call the methods below.

#include <firmir/object.hpp>
#include <firmir/string.hpp>
#include <firmir/timer.hpp>

#include <string>
#include <vector>

namespace Firmir {
namespace Gui {
class Image;
class ListModel;
} // namespace Gui
namespace Multimedia {
class VideoOutput;
class VideoSource;
} // namespace Multimedia
} // namespace Firmir

class PlayerApp : public Firmir::Object {
public:
    explicit PlayerApp(Object* parent = nullptr);
    ~PlayerApp() override;

    /// Wires the FML-created views and opens the starting directory.
    /// @p path may point to a single media file: its directory is opened
    /// and the file is activated right away.
    void attach(const std::string& path, Firmir::Multimedia::VideoOutput* video,
                Firmir::Gui::Image* photo, Firmir::Gui::ListModel* fileModel);

    // ---- called from the Firml handlers ----
    /// Opens a typed-in path: directories are entered, media files are
    /// played, images are shown (the browser navigates to the parent).
    void openPath(Firmir::String path);
    void showPage(double page); // 0 = browser, 1 = video, 2 = photos
    /// Enters a directory, plays a media file or shows an image.
    void activate(double index);
    /// Navigates to the parent directory.
    void goUp();
    void play();
    void pause();
    void stop();
    /// Seeks to @p seconds (clamped to [0, duration]); a no-op without a
    /// playing source or when the duration is unknown.
    void seek(double seconds);
    void nextTrack(); ///< next audio/video entry in the current directory
    void prevTrack();

private:
    enum class Kind { Directory, Audio, Video, ImageFile };

    struct Entry {
        std::string path;  // absolute
        std::string label; // display name
        Kind kind;
    };

    void refreshDirectory();  ///< rebuilds the model from m_dir
    void playFile(int index); ///< opens and plays an audio/video entry
    void showImage(int index);///< shows an image entry on the photos page
    void stepMedia(int direction); ///< next/prev media entry in the listing
    void updatePositionText();

    std::vector<Entry> m_entries;
    std::string m_dir;       // current directory (absolute)
    int m_current = -1;      // entry index of the playing media, -1 = none
    Firmir::Multimedia::VideoOutput* m_video = nullptr;  // not owned
    Firmir::Gui::Image* m_photo = nullptr;               // not owned
    Firmir::Gui::ListModel* m_model = nullptr;           // not owned
    Firmir::Multimedia::VideoSource* m_source = nullptr; // owned (child)
    Firmir::Timer m_positionTimer{this};
};
