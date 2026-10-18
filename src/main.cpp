// SKYFALL — Real-Time Combat Flight Simulator & Computer Vision Laboratory (prototype).
//
// Usage:
//   skyfall                      normal start (title screen)
//   skyfall --mode arena|m01     jump straight into the Dogfight Arena / Mission 01
//   skyfall --capture <dir>      scripted screenshots from the running renderer, then exit
//   skyfall --bench <seconds>    scripted benchmark; writes benchmarks/benchmark.json + frame_times.csv
//   skyfall --size 1280x720 --windowed --quality 0|1|2
#include "app/Game.hpp"
#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <cstdarg>
#include <filesystem>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if !defined(__EMSCRIPTEN__) && (defined(__APPLE__) || defined(__unix__))
#include <csignal>
#include <exception>
#include <execinfo.h>
#include <unistd.h>
#define SKYFALL_SIGNALS 1
#endif

namespace {
FILE* g_log = nullptr;

#ifdef SKYFALL_SIGNALS
// If the game is ever stopped by a signal, say which one (and where) in logs/skyfall.log and on
// the terminal, so a crash or a forced stop on a player's machine can be diagnosed afterwards.
void writeBoth(const char* s) {
    (void)!write(2, s, std::strlen(s));
    if (g_log) (void)!write(fileno(g_log), s, std::strlen(s));
}
void fatalSignal(int sig) {
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE"
                     : sig == SIGABRT ? "SIGABRT" : sig == SIGTERM ? "SIGTERM" : sig == SIGINT ? "SIGINT" : sig == SIGHUP ? "SIGHUP"
                     : sig == SIGTSTP ? "SIGTSTP" : "signal";
    if (g_log) std::fflush(g_log);
    writeBoth("ERROR: SKYFALL: stopped by ");
    writeBoth(name);
    writeBoth("\n");
    if (sig != SIGINT && sig != SIGTERM && sig != SIGHUP && sig != SIGTSTP) {
        void* frames[48];
        int n = backtrace(frames, 48);
        backtrace_symbols_fd(frames, n, 2);
        if (g_log) backtrace_symbols_fd(frames, n, fileno(g_log));
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
void installSignalHandlers() {
    // terminal job control and broken pipes must never stop the game
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    for (int s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGINT, SIGHUP, SIGTSTP}) signal(s, fatalSignal);
    std::set_terminate([] {
        writeBoth("ERROR: SKYFALL: uncaught exception\n");
        std::abort();
    });
}
#endif
// Mirror raylib's log to the console and to logs/skyfall.log (warnings, errors and GPU info),
// so problems on a user's machine can be diagnosed after the window is closed.
void logCallback(int level, const char* fmt, va_list args) {
    char buf[1024];
    std::vsnprintf(buf, sizeof buf, fmt, args);
    const char* tag = level >= LOG_ERROR ? "ERROR" : level == LOG_WARNING ? "WARNING" : "INFO";
    std::fprintf(stdout, "%s: %s\n", tag, buf);
    bool keep = level >= LOG_WARNING || std::strstr(buf, "GL:") || std::strstr(buf, "SKYFALL") ||
                std::strstr(buf, "Display size") || std::strstr(buf, "> Vendor") || std::strstr(buf, "> Renderer") || std::strstr(buf, "> Version") || std::strstr(buf, "Render size") || std::strstr(buf, "Screen size");
    if (g_log && keep) { std::fprintf(g_log, "%s: %s\n", tag, buf); std::fflush(g_log); }
}
}  // namespace

static int skyfallMain(int argc, char** argv) {
    std::error_code ec;
    std::filesystem::create_directories("logs", ec);
    g_log = std::fopen("logs/skyfall.log", "w");
#ifdef SKYFALL_SIGNALS
    installSignalHandlers();
#endif
    SetTraceLogCallback(logCallback);
    sky::Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--capture") opt.captureDir = next();
        else if (a == "--bench") opt.benchSeconds = std::atof(next().c_str());
        else if (a == "--mode") opt.startMode = next();
        else if (a == "--windowed") opt.windowed = true;
        else if (a == "--native") opt.native = true;
        else if (a == "--quality") opt.quality = std::atoi(next().c_str());
        else if (a == "--size") {
            std::string v = next();
            if (std::sscanf(v.c_str(), "%dx%d", &opt.width, &opt.height) != 2) { opt.width = opt.height = 0; }
        } else if (a == "--help" || a == "-h") {
            std::printf("skyfall [--mode arena|m01] [--capture dir] [--bench seconds] [--size WxH] [--windowed] [--quality 0-2]\n");
            return 0;
        }
    }
#ifdef __EMSCRIPTEN__
    // Browser build: index.html?mode=arena (or m01) jumps straight into a mode.
    if (const char* m = emscripten_run_script_string(
            "(function(){var m=new URLSearchParams(location.search).get('mode')||'';return /^(arena|[mc][0-9]{2})$/.test(m)?m:'';})()"))
        if (*m) opt.startMode = m;
    // Browser build: the game lives for the whole page; Game::run hands control to the
    // browser's animation loop and never returns, so it must not live on the stack.
    opt.windowed = true;
    static sky::Game* game = new sky::Game();
    return game->run(opt);
#else
    sky::Game game;
    int rc = game.run(opt);
    if (g_log) std::fclose(g_log);
    return rc;
#endif
}

#ifdef __EMSCRIPTEN__
int main() {
    static char name[] = "skyfall";
    static char* argv[] = {name, nullptr};
    return skyfallMain(1, argv);
}
#else
int main(int argc, char** argv) { return skyfallMain(argc, argv); }
#endif
