#include "karevona/metrics.hpp"

namespace karevona {

void InMemoryMetrics::counter_add(const std::string& name, double delta, const MetricLabels& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[{name, labels}] += delta;
}
void InMemoryMetrics::gauge_set(const std::string& name, double value, const MetricLabels& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[{name, labels}] = value;
}
void InMemoryMetrics::histogram_observe(const std::string& name, double value, const MetricLabels& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& h = histograms_[{name, labels}];
    h.count += 1;
    h.sum += value;
}
double InMemoryMetrics::counter(const std::string& name, const MetricLabels& labels) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find({name, labels});
    return it == counters_.end() ? 0.0 : it->second;
}
double InMemoryMetrics::gauge(const std::string& name, const MetricLabels& labels) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find({name, labels});
    return it == gauges_.end() ? 0.0 : it->second;
}
uint64_t InMemoryMetrics::histogram_count(const std::string& name, const MetricLabels& labels) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = histograms_.find({name, labels});
    return it == histograms_.end() ? 0 : it->second.count;
}

}  // namespace karevona
