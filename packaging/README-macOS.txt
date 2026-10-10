SKYFALL for macOS (Apple Silicon and Intel, macOS 12 or newer)

1. Unzip SKYFALL-macOS.zip.
2. Right-click "Play-SKYFALL.command" and choose Open, then Open again.
   (The build is not notarised by Apple, so macOS asks once. After that a double-click works.)

If macOS still refuses, open Terminal in this folder and run:
   xattr -dr com.apple.quarantine . && ./skyfall

Controls: W/S throttle (full = afterburner), arrow keys pitch and roll, A/D yaw,
double-tap left/right for an aileron roll, Space cannon, F missile, Q swap ATA/ATS,
X flares, C camera, H help, Esc pause, Cmd+Q quit. logs/skyfall.log records frame times.
