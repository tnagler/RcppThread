#pragma once

#include <cassert>
#include <chrono>
#include <vector>

namespace bench {

double
time_one(const std::function<void()>& f)
{
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

void
time_once(const std::vector<std::function<void()>>& funcs,
          std::vector<double>& times)
{
    for (size_t k = 0; k < funcs.size(); k++) {
        times[k] += time_one(funcs[k]);
    }
}

std::vector<double>
benchmark(std::vector<std::function<void()>> funcs, size_t min_sec = 10)
{
    size_t min_time = 0;
    std::vector<double> times(funcs.size(), min_sec);
    while (min_time < min_sec) {
        time_once(funcs, times);
        min_time = *std::min_element(times.begin(), times.end());
    }
    return times;
}

}
