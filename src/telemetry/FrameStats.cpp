#include "FrameStats.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>

namespace sky {

void FrameStats::push(double ms) {
    if (ring_.size() < cap_) ring_.push_back(ms);
    else { ring_[head_] = ms; head_ = (head_ + 1) % cap_; }
    if (keepAll_) all_.push_back(ms);
}

Percentiles FrameStats::computeFrom(std::vector<double> v) {
    Percentiles p;
    p.samples = v.size();
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    auto pct = [&](double q) {
        double idx = q * (v.size() - 1);
        size_t lo = static_cast<size_t>(std::floor(idx)), hi = static_cast<size_t>(std::ceil(idx));
        return v[lo] + (v[hi] - v[lo]) * (idx - lo);
    };
    p.p50 = pct(0.50); p.p95 = pct(0.95); p.p99 = pct(0.99);
    p.mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    p.max = v.back();
    return p;
}

Percentiles FrameStats::compute() const { return computeFrom(ring_); }

bool FrameStats::writeCsv(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "frame,ms\n";
    const auto& v = keepAll_ ? all_ : ring_;
    for (size_t i = 0; i < v.size(); ++i) f << i << ',' << v[i] << '\n';
    return static_cast<bool>(f);
}

bool FrameStats::writeJson(const std::string& path, const std::string& label, int w, int h, const std::string& renderer) const {
    std::ofstream f(path);
    if (!f) return false;
    Percentiles p = computeFrom(keepAll_ ? all_ : ring_);
    f << "{\n  \"label\": \"" << label << "\",\n  \"resolution\": [" << w << ", " << h << "],\n"
      << "  \"renderer\": \"" << renderer << "\",\n  \"samples\": " << p.samples << ",\n"
      << "  \"frame_ms\": {\"mean\": " << p.mean << ", \"p50\": " << p.p50 << ", \"p95\": " << p.p95
      << ", \"p99\": " << p.p99 << ", \"max\": " << p.max << "},\n"
      << "  \"fps_mean\": " << (p.mean > 0 ? 1000.0 / p.mean : 0) << "\n}\n";
    return static_cast<bool>(f);
}

}  // namespace sky
