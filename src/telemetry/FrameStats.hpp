// Frame-time statistics (p50/p95/p99) and a CSV/JSON benchmark exporter.
#pragma once

#include <string>
#include <vector>

namespace sky {

struct Percentiles {
    double p50 = 0, p95 = 0, p99 = 0, mean = 0, max = 0;
    size_t samples = 0;
};

class FrameStats {
public:
    explicit FrameStats(size_t capacity = 600) : cap_(capacity) { ring_.reserve(capacity); }
    void push(double frameMs);
    Percentiles compute() const;
    const std::vector<double>& all() const { return all_; }
    void keepAll(bool on) { keepAll_ = on; }
    /// Writes benchmark results; returns false on I/O error.
    bool writeCsv(const std::string& path) const;
    bool writeJson(const std::string& path, const std::string& label, int width, int height,
                   const std::string& renderer) const;
    static Percentiles computeFrom(std::vector<double> v);

private:
    size_t cap_;
    size_t head_ = 0;
    std::vector<double> ring_;
    std::vector<double> all_;
    bool keepAll_ = false;
};

}  // namespace sky
