# firmir-player

File browser + media player over firmir-multimedia. The **Files** tab lists
the current directory (subdirs first, `[..]` entry to go up, hidden files
skipped, non-media files filtered out); clicking a directory enters it,
clicking a media file plays it. Music plays through the available
VideoSource backend (gstreamer, then ffmpeg) with
prev/play/pause/stop/next, a position readout and auto-advance to the next
track; the seek bar above the transport buttons shows the progress and
clicking/dragging it jumps to the position (both backends support seeking);
video files switch to the Video page (VideoOutput) automatically —
if the gstreamer backend cannot open a file, playback falls back to the
ffmpeg backend (and vice versa). Image files open on the Photos page
(firmir-gui `Image`, aspect-preserved). The path bar at the top accepts a
directory or a media file path (a file opens its directory and starts
playing).

```
cmake -S . -B build && cmake --build build -j"$(nproc)"   # pkg-config firmir-multimedia, else builds ../firmir
./build/firmir-player [dir-or-file]             # default: ~/Music, fallback ".";
                                                # a file opens its dir and plays
```

Playback needs a decode backend: install `libgstreamer1.0-dev
libgstreamer-plugins-base1.0-dev` (audio+video) or `libavformat-dev
libavcodec-dev libavutil-dev libswscale-dev` (video only) and rebuild
firmir-multimedia. Without one the app still runs and reports
"no decode backend".
