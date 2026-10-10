#pragma once
// All SKYFALL shaders are written as GLSL 3.30. The WebAssembly build runs on WebGL 2, which
// takes GLSL ES 3.00: same language level, different header and explicit default precision.
#include "raylib.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace sky {

inline std::string toGlslEs(std::string s) {
    const std::string from = "#version 330";
    size_t p = s.find(from);
    if (p != std::string::npos) {
        size_t eol = s.find('\n', p);
        s.replace(p, (eol == std::string::npos ? s.size() : eol) - p,
                  "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;");
    }
    return s;
}

inline std::string toTargetGlsl(const char* src) {
    std::string s = src ? src : "";
#ifdef __EMSCRIPTEN__
    s = toGlslEs(s);
#endif
    return s;
}

inline Shader loadShaderGlsl(const char* vs, const char* fs) {
#ifndef __EMSCRIPTEN__
    if (const char* dir = std::getenv("SKYFALL_DUMP_GLSL")) {  // dev aid: dump ES variants to validate
        static int n = 0;
        ++n;
        for (int k = 0; k < 2; ++k) {
            const char* src = k ? fs : vs;
            if (!src) continue;
            std::string path = std::string(dir) + "/s" + std::to_string(n) + (k ? ".frag" : ".vert");
            if (FILE* f = std::fopen(path.c_str(), "w")) { std::fputs(toGlslEs(src).c_str(), f); std::fclose(f); }
        }
    }
#endif
    std::string v = toTargetGlsl(vs), f = toTargetGlsl(fs);
    return LoadShaderFromMemory(vs ? v.c_str() : nullptr, fs ? f.c_str() : nullptr);
}

}  // namespace sky
