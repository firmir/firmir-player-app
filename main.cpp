// firmir-player: a small file browser/player — navigates directories,
// plays music/video files through the firmir-multimedia VideoSource
// backends (gstreamer/ffmpeg) and shows images. The UI is declared in
// player.fml (compiled by firmlc); the logic lives in PlayerApp.
//
//   firmir-player [dir-or-file]  (default: ~/Music, falling back to ".";
//   a single file opens its directory and starts playing right away)
//
// Backend selection via FIRMIR_GUI_DISPLAY / FIRMIR_GUI_INPUT /
// FIRMIR_GUI_PLUGIN_PATH, like every firmir-gui app.

#include "player.hpp"

#include "PlayerApp.hpp"

#include <firmir/gui/listmodel.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/log.hpp>
#include <firmir/multimedia/videooutput.hpp>
#include <firmir/thread.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

int main(int argc, char** argv) {
    std::filesystem::path mediaPath;
    if (argc > 1)
        mediaPath = argv[1];
    else {
        const char* home = std::getenv("HOME");
        mediaPath = std::filesystem::path(home ? home : ".") / "Music";
        if (!std::filesystem::is_directory(mediaPath))
            mediaPath = ".";
    }

    auto* platform = Firmir::Gui::Platform::instance();
    if (platform->screens().empty()) {
        std::fprintf(stderr, "no screens — check FIRMIR_GUI_DISPLAY / "
                             "FIRMIR_GUI_PLUGIN_PATH\n");
        return 1;
    }

    // Creates the window, the whole UI tree and the PlayerApp logic object,
    // then shows the window (see player.fml).
    PlayerObjects objects = createPlayer(platform->screens().front());
    objects.player->attach(mediaPath.string(), objects.video, objects.photo,
                           objects.fileModel);
    objects.root->connect<bool>(
        objects.root, Firmir::Gui::Window::signalVisibilityChanged,
        [](bool visible) {
            if (!visible)
                Firmir::Thread::mainThread()->quit();
        });

    const int rc = Firmir::Thread::mainThread()->exec();
    Firmir::logInfo() << "firmir-player: bye";
    delete objects.root; // owns the whole tree (including the source)
    return rc;
}
