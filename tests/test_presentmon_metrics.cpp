#include "presentmon_metrics.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>

int main()
{
    const char* path = "presentmon-test.csv";
    {
        std::ofstream out(path);
        out << "TimeInSeconds,MsBetweenPresents,MsAllInputToPhotonLatency,MsClickToPhotonLatency,MsCPUBusy,MsGPUActive\n";
        for (int i = 0; i < 250; ++i)
            out << i / 100.0 << ',' << (i == 200 ? 40.0 : 10.0) << ','
                << (5.0 + i / 100.0) << ",7,3,4\n";
    }
    const auto metrics = parse_presentmon_file(path);
    std::remove(path);
    bool okay = metrics.state == PresentMonDataState::Valid &&
        metrics.rows == 250 && metrics.valid_frames == 250 &&
        metrics.input.samples == 250 && metrics.click.samples == 250 &&
        metrics.coverage_seconds > 2.48 && metrics.stutters == 1 &&
        metrics.input.p99 > 7.4 && metrics.average_fps > 98.0;
    {
        std::ofstream out(path); out << "Unknown,Other\n1,2\n";
    }
    okay = parse_presentmon_file(path).state == PresentMonDataState::UnsupportedSchema && okay;
    std::remove(path);
    if (!okay) std::cerr << "PresentMon metrics test failed\n";
    return okay ? 0 : 1;
}
