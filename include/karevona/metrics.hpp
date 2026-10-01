// Metrics abstraction. An OpenTelemetry-backed implementation is a future
// adapter; core code only sees IMetrics.
#pragma once

#include <map>
#include <mutex>
#include <string>

namespace karevona {

using MetricLabels = std::map<std::string, std::string>;

class IMetrics {
public:
    virtual ~IMetrics() = default;
    virtual void counter_add(const std::string& name, double delta, const MetricLabels& labels = {}) = 0;
    virtual void gauge_set(const std::string& name, double value, const MetricLabels& labels = {}) = 0;
    virtual void histogram_observe(const std::string& name, double value, const MetricLabels& labels = {}) = 0;
};

class NullMetrics final : public IMetrics {
public:
    void counter_add(const std::string&, double, const MetricLabels&) override {}
    void gauge_set(const std::string&, double, const MetricLabels&) override {}
    void histogram_observe(const std::string&, double, const MetricLabels&) override {}
};

// Simple in-process registry (also used by tests).
class InMemoryMetrics final : public IMetrics {
public:
    void counter_add(const std::string& name, double delta, const MetricLabels& labels = {}) override;
    void gauge_set(const std::string& name, double value, const MetricLabels& labels = {}) override;
    void histogram_observe(const std::string& name, double value, const MetricLabels& labels = {}) override;

    double counter(const std::string& name, const MetricLabels& labels = {}) const;
    double gauge(const std::string& name, const MetricLabels& labels = {}) const;
    // Number of observations recorded for a histogram.
    uint64_t histogram_count(const std::string& name, const MetricLabels& labels = {}) const;

private:
    struct HistogramData {
        uint64_t count = 0;
        double sum = 0;
    };
    using Key = std::pair<std::string, MetricLabels>;
    mutable std::mutex mutex_;
    std::map<Key, double> counters_;
    std::map<Key, double> gauges_;
    std::map<Key, HistogramData> histograms_;
};

}  // namespace karevona
