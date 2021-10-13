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
    std::atomic_size_t it_{0};
    size_t numIt_;
    size_t printEvery_;
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
        auto end = (it + 1 == numIt_) ? ".\n" : "";
        Rcout << "\rCalculating: " << pct << "%" << end;
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
        msg << "Calculating: [";
        int i = 0;
        for (; i < pct / 100.0 * 50; i++)
            msg << "=";
        for (; i < 50; i++)
            msg << " ";
        msg << "] " << pct << "%   \r";
        if (it == numIt_)
            msg << "\n";
        Rcout << msg.str();
    }
};


}
