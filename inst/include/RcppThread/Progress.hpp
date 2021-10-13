// Copyright © 2021 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include "RcppThread/Rcout.hpp"
#include <atomic>
#include <chrono>
#include <sstream>
#include <cmath>

namespace RcppThread {

class ProgressPrinter {
public:
    ProgressPrinter(size_t numIt, size_t printEvery)
        : numIt_(numIt)
        , printEvery_(printEvery)
    {}

    // need to be defined in child classes
    virtual bool needsPrint(size_t it) = 0;
    virtual void printProgress(size_t it) = 0;

    // pre-increment operator
    size_t operator++ () {
        size_t it = ++it_;
        if (this->needsPrint(it))
            this->printProgress(it);
        return it;
    }

    // post-increment operator
    size_t operator++ (int) {
        size_t it = it_++;
        if (this->needsPrint(it))
            this->printProgress(it);
        return it;
    }

protected:
    float remainingSecs() {
        using namespace std::chrono;
        duration<float> timeDiff = system_clock::now() - startTime_;
        auto secs = (numIt_ - it_) * timeDiff.count() / it_;
        return std::floor(secs);
    }

    std::string remaingTimeString(size_t it) {
        std::stringstream msg;
        if (it == 0) {
            startTime_ = std::chrono::system_clock::now();
        } else if (it + 1 == numIt_) {
            msg << "(done)                         \n";
        } else {
            std::chrono::duration<float> timeDiff =
                std::chrono::system_clock::now() - startTime_;
            auto secs = (numIt_ - it_) * timeDiff.count() / it_;
            msg << "(~" << std::floor(secs) << "s remaining)       ";
        }
        return msg.str();
    }

    size_t numIt_;
    size_t printEvery_;
    std::atomic_size_t it_{0};
    std::chrono::time_point<std::chrono::system_clock> startTime_;
    bool is_done_{false};
};


class ProgressCounter : public ProgressPrinter {

public:
    ProgressCounter(size_t numIt, size_t printEvery) :
    ProgressPrinter(numIt, printEvery)
    {}

private:
    bool needsPrint(size_t it) {
        it = it + 1;
        return (it % printEvery_ == 0) || (it == numIt_);
    }

    void printProgress(size_t it) {
        double pct = std::round((it + 1) * 100.0 / numIt_);
        std::stringstream msg;
        msg << "\rCalculating: " << pct << "% " << remaingTimeString(it);
        Rcout << msg.str();
    }
};

class ProgressBar : public ProgressPrinter {

public:
    ProgressBar(size_t numIt, size_t printEvery) :
    ProgressPrinter(numIt, printEvery)
    {}

private:
    bool needsPrint(size_t it) {
        it = it + 1;
        return (it % printEvery_ == 0) || (it == numIt_);
    }

    void printProgress(size_t it) {
        size_t pct = (it + 1) * 100 / numIt_;
        std::stringstream msg;
        msg << "\rCalculating: [";
        int i = 0;
        for (; i < pct / 100.0 * 40; i++)
            msg << "=";
        for (; i < 40; i++)
            msg << " ";
        msg << "] " << pct << "% " << remaingTimeString(it);
        Rcout << msg.str();
    }
};


}
