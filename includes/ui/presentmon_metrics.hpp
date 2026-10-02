#pragma once

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

enum class PresentMonDataState { Valid, NoFile, Empty, UnsupportedSchema, NoValidSamples };

struct MetricSummary {
    size_t samples = 0;
    double mean = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

struct PresentMonMetrics {
    PresentMonDataState state = PresentMonDataState::NoFile;
    size_t rows = 0;
    size_t valid_frames = 0;
    double coverage_seconds = 0.0;
    MetricSummary input;
    MetricSummary click;
    MetricSummary frame;
    MetricSummary cpu;
    MetricSummary gpu;
    double average_fps = 0.0;
    double one_percent_low_fps = 0.0;
    double frame_cv_percent = 0.0;
    size_t stutters = 0;
};

inline std::vector<std::string> presentmon_csv_fields(const std::string& line)
{
    std::vector<std::string> result;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"'); ++i;
            } else quoted = !quoted;
        } else if (c == ',' && !quoted) {
            result.push_back(field); field.clear();
        } else field.push_back(c);
    }
    result.push_back(field);
    return result;
}

inline bool presentmon_number(const std::string& text, double& value)
{
    try {
        size_t used = 0;
        value = std::stod(text, &used);
        return used == text.size() && std::isfinite(value) && value >= 0.0;
    } catch (...) { return false; }
}

inline MetricSummary summarize_metric(std::vector<double> values)
{
    MetricSummary out;
    if (values.empty()) return out;
    out.samples = values.size();
    double total = 0.0;
    for (double value : values) total += value;
    out.mean = total / values.size();
    std::sort(values.begin(), values.end());
    const auto percentile = [&](double p) {
        const double position = p * static_cast<double>(values.size() - 1);
        const size_t low = static_cast<size_t>(position);
        const size_t high = (std::min)(low + 1, values.size() - 1);
        return values[low] + (values[high] - values[low]) * (position - low);
    };
    out.median = percentile(0.50);
    out.p95 = percentile(0.95);
    out.p99 = percentile(0.99);
    return out;
}

inline PresentMonMetrics parse_presentmon_file(const std::string& path)
{
    PresentMonMetrics result;
    std::ifstream file(path);
    std::string line;
    if (!file) return result;
    if (!std::getline(file, line)) { result.state = PresentMonDataState::Empty; return result; }
    auto headers = presentmon_csv_fields(line);
    if (!headers.empty() && headers[0].size() >= 3 &&
        static_cast<unsigned char>(headers[0][0]) == 0xef)
        headers[0].erase(0, 3);
    std::unordered_map<std::string, size_t> columns;
    for (size_t i = 0; i < headers.size(); ++i) columns[headers[i]] = i;
    const auto column = [&](std::initializer_list<const char*> names) -> size_t {
        for (const char* name : names) {
            const auto found = columns.find(name);
            if (found != columns.end()) return found->second;
        }
        return std::numeric_limits<size_t>::max();
    };
    const size_t frame_col = column({"MsBetweenPresents", "MsBetweenDisplayChange"});
    const size_t input_col = column({"MsAllInputToPhotonLatency"});
    const size_t click_col = column({"MsClickToPhotonLatency"});
    const size_t cpu_col = column({"MsCPUFrameTime", "MsCPUBusy"});
    const size_t gpu_col = column({"MsGPUActive", "MsGPUTime", "MsGPUVideoActive"});
    const size_t time_col = column({"TimeInSeconds"});
    if (frame_col == std::numeric_limits<size_t>::max() &&
        input_col == std::numeric_limits<size_t>::max()) {
        result.state = PresentMonDataState::UnsupportedSchema;
        return result;
    }
    std::vector<double> frame, input, click, cpu, gpu;
    double first_time = 0.0, last_time = 0.0;
    bool have_time = false;
    const auto take = [](const std::vector<std::string>& fields, size_t index,
                         std::vector<double>& values, bool require_positive = true) {
        if (index >= fields.size()) return;
        double value = 0.0;
        if (presentmon_number(fields[index], value) && (!require_positive || value >= 0.0) &&
            (!require_positive || value > 0.0)) values.push_back(value);
    };
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        ++result.rows;
        const auto fields = presentmon_csv_fields(line);
        take(fields, frame_col, frame);
        take(fields, input_col, input);
        take(fields, click_col, click);
        take(fields, cpu_col, cpu);
        take(fields, gpu_col, gpu);
        if (time_col < fields.size()) {
            double value = 0.0;
            if (presentmon_number(fields[time_col], value)) {
                if (!have_time) first_time = value;
                last_time = value; have_time = true;
            }
        }
    }
    result.frame = summarize_metric(frame);
    result.input = summarize_metric(input);
    result.click = summarize_metric(click);
    result.cpu = summarize_metric(cpu);
    result.gpu = summarize_metric(gpu);
    result.valid_frames = frame.size();
    if (have_time && last_time >= first_time) result.coverage_seconds = last_time - first_time;
    else for (double value : frame) result.coverage_seconds += value / 1000.0;
    if (!frame.empty()) {
        result.average_fps = result.frame.mean > 0.0 ? 1000.0 / result.frame.mean : 0.0;
        result.one_percent_low_fps = result.frame.p99 > 0.0 ? 1000.0 / result.frame.p99 : 0.0;
        double variance = 0.0;
        for (double value : frame) variance += (value - result.frame.mean) * (value - result.frame.mean);
        result.frame_cv_percent = result.frame.mean > 0.0 ?
            std::sqrt(variance / frame.size()) * 100.0 / result.frame.mean : 0.0;
        const double threshold = (std::max)(result.frame.median * 1.5,
                                             result.frame.median + 5.0);
        result.stutters = static_cast<size_t>(std::count_if(frame.begin(), frame.end(),
            [&](double value) { return value > threshold; }));
    }
    result.state = (result.frame.samples || result.input.samples) ?
        PresentMonDataState::Valid : PresentMonDataState::NoValidSamples;
    return result;
}

inline std::string presentmon_compact_text(const PresentMonMetrics& metrics)
{
    if (metrics.state == PresentMonDataState::NoFile) return "PresentMon data unavailable";
    if (metrics.state == PresentMonDataState::Empty) return "PresentMon output empty";
    if (metrics.state == PresentMonDataState::UnsupportedSchema) return "PresentMon schema unsupported";
    if (metrics.state == PresentMonDataState::NoValidSamples) return "PresentMon samples invalid";
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    if (metrics.input.samples)
        out << "Input " << metrics.input.median << " ms  p95 " << metrics.input.p95;
    else out << "Input n/a";
    if (metrics.click.samples) out << "  Click " << metrics.click.median << " ms";
    if (metrics.frame.samples)
        out << " | " << metrics.average_fps << " FPS  p99 " << metrics.frame.p99
            << " ms  stutter " << metrics.stutters;
    out << " | " << metrics.valid_frames << " samples/" << metrics.coverage_seconds << " s";
    return out.str();
}
