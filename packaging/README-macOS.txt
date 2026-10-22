SKYFALL for macOS (Apple Silicon and Intel, macOS 12 or newer)

1. Unzip SKYFALL-macOS.zip.
2. Drag SKYFALL.app into Applications (or keep it anywhere you like).
3. The first time: right-click SKYFALL.app and choose Open, then Open again.
   (The download is not notarised by Apple, so macOS asks once. After that a double-click works,
   and you can drag it to the Dock or make a Desktop alias: right-click > Make Alias.)

If macOS still refuses, open Terminal and run:
   xattr -dr com.apple.quarantine /Applications/SKYFALL.app

Building from source instead? In the project folder run  ./scripts/make_app.sh  : it builds the
Release once, installs ~/Applications/SKYFALL.app and puts a SKYFALL shortcut on the Desktop.

Your settings, progress, downloaded maps, logs and screenshots live in
   ~/Library/Application Support/SKYFALL
so they survive updates of the app.

The folder version (Play-SKYFALL.command next to the skyfall program) still works as before.

Controls: W/S throttle (full = afterburner), arrow keys pitch and roll, A/D yaw,
double-tap left/right for an aileron roll, Space cannon, F missile, Q swap ATA/ATS,
X flares, C camera, H help, Esc pause, Cmd+Q quit.
