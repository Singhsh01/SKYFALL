#pragma once
// Row-parallel loop used by the terrain build and the terrain-data/shadow bakes.
// Native builds split rows across hardware threads; the WebAssembly build runs them inline
// (browser threads need cross-origin isolation headers that static hosting does not provide).
#include <algorithm>
#include <thread>
#include <vector>

namespace sky {

template <class Fn>
void parallelRows(int rows, Fn&& fn) {
#ifdef __EMSCRIPTEN__
    for (int j = 0; j < rows; ++j) fn(j);
#else
    unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    if (threads == 1) { for (int j = 0; j < rows; ++j) fn(j); return; }
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&, t] { for (int j = static_cast<int>(t); j < rows; j += static_cast<int>(threads)) fn(j); });
    for (auto& th : pool) th.join();
#endif
}

}  // namespace sky
