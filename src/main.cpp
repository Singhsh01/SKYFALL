// SKYFALL — Real-Time Combat Flight Simulator & Computer Vision Laboratory (prototype).
//
// Usage:
//   skyfall                      normal start (title screen)
//   skyfall --mode arena|m01     jump straight into the Dogfight Arena / Mission 01
//   skyfall --capture <dir>      scripted screenshots from the running renderer, then exit
//   skyfall --bench <seconds>    scripted benchmark; writes benchmarks/benchmark.json + frame_times.csv
//   skyfall --size 1280x720 --windowed --quality 0|1|2
#include "app/Game.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    sky::Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--capture") opt.captureDir = next();
        else if (a == "--bench") opt.benchSeconds = std::atof(next().c_str());
        else if (a == "--mode") opt.startMode = next();
        else if (a == "--windowed") opt.windowed = true;
        else if (a == "--quality") opt.quality = std::atoi(next().c_str());
        else if (a == "--size") {
            std::string v = next();
            if (std::sscanf(v.c_str(), "%dx%d", &opt.width, &opt.height) != 2) { opt.width = opt.height = 0; }
        } else if (a == "--help" || a == "-h") {
            std::printf("skyfall [--mode arena|m01] [--capture dir] [--bench seconds] [--size WxH] [--windowed] [--quality 0-2]\n");
            return 0;
        }
    }
    sky::Game game;
    return game.run(opt);
}
