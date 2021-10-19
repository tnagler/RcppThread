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
        , startTime_(std::chrono::steady_clock::now())
    {}

    // need to be defined in child classes
    virtual bool needsPrint(size_t it) = 0;
    virtual void printProgress(size_t it) = 0;

    // pre-increment operator
    size_t operator++ () {
        size_t it = it_++;
        if (this->needsPrint(it))
            this->printProgress(it);
        return it + 1;
    }

    // post-increment operator
    size_t operator++ (int) {
        size_t it = it_++;
        if (this->needsPrint(it))
            this->printProgress(it);
        return it;
    }

protected:
    size_t remainingSecs() {
        using namespace std::chrono;
        auto diff = duration<float>(steady_clock::now() - startTime_).count();
        auto remaining = (numIt_ - it_) * diff / it_;
        return static_cast<size_t>(remaining);
    }

    std::string remaingTimeString(size_t it) {
        std::ostringstream msg;
        if (it == 0) {
            startTime_ = std::chrono::steady_clock::now();
        } else if (it + 1 == numIt_) {
            msg << "(done)                         \n";
        } else {
            msg << "(~" << formatTime(remainingSecs()) << " remaining)       ";
        }
        return msg.str();
    }

    std::string formatTime(size_t secs) {
        std::ostringstream msg;
        constexpr size_t minute = 60;
        constexpr size_t hour = 60 * minute;
        constexpr size_t day = 24 * hour;
        if (secs / day > 0) {
            msg << secs / day << "d";
            secs = secs % day;
        }
        if (secs / hour > 0) {
            msg << secs / hour << "h";
            secs = secs % hour;
        }
        if (secs / minute > 0) {
            msg << secs / minute << "m";
            secs = secs % minute;
        }
        msg << secs << "s";
        return msg.str();
    }

    size_t numIt_;
    size_t printEvery_;
    std::atomic_size_t it_{0};
    std::chrono::time_point<std::chrono::steady_clock> startTime_;
    std::atomic_bool is_done_{false};
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
        std::ostringstream msg;
        msg << "\rCalculating: " << pct << "% " << remaingTimeString(it);
        if (!is_done_) {
            if (it + 1 == numIt_)
                is_done_ = true;
            Rcout << msg.str();
        }
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
        std::ostringstream msg;
        msg << "\rCalculating: " <<
            makeBar(pct) << pct << "% " <<
            remaingTimeString(it);
        if (!is_done_) {
            if (it + 1 == numIt_)
                is_done_ = true;
            Rcout << msg.str();
        }
    }

    std::string makeBar(size_t pct, size_t numBars = 40) {
        std::ostringstream msg;
        msg << "[";
        int i = 0;
        for (; i < pct / 100.0 * numBars; i++)
            msg << "=";
        for (; i < numBars; i++)
            msg << " ";
        msg << "] ";
        return msg.str();
    }
};


}
