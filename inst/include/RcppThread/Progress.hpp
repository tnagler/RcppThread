// Copyright © 2022 Thomas Nagler
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

//! @brief Abstract class for printing progress.
//!
//! This class contains most of the logic for tracking progress in a parallel
//! loop. Child classes must define a method `void printProgress()` that is
//! called whenever an update is required.
class ProgressPrinter {
public:
    //! Constructor for abstract class `ProgressPrinter`.
    //! @param numIt total number of iterations.
    //! @param printEvery how regularly to print updates (in seconds).
    ProgressPrinter(size_t numIt, size_t printEvery = 1)
        : numIt_(numIt)
        , printEvery_(printEvery)
        , startTime_(std::chrono::steady_clock::now())
    {}

    //! prints progress whenever an update is necessary.
    virtual void printProgress() = 0;

    //! pre-increment operator
    size_t operator++ () {
        size_t it = it_++;
        if (needsPrint())
            printProgress();
        return it + 1;
    }

    //! post-increment operator
    size_t operator++ (int) {
        size_t it = it_++;
        if (needsPrint())
            printProgress();
        return it;
    }

protected:
    //! checks whether it's time for an update.
    bool needsPrint() {
        using namespace std::chrono;
        auto passed = duration<float>(steady_clock::now() - startTime_).count();
        bool needsUpdate = (passed / printEvery_ > numUpdates_ + 1);
        needsUpdate = needsUpdate || (it_ == numIt_); // always print when done
        if (needsUpdate)
            numUpdates_++;
        return needsUpdate;
    }

    //! estimates the remaining time in seconds.
    size_t remainingSecs() {
        using namespace std::chrono;
        auto diff = duration<float>(steady_clock::now() - startTime_).count();
        auto remaining = (numIt_ - it_) * diff / it_;
        return static_cast<size_t>(remaining);
    }

    //! prints either remaining time or that the computation is done.
    std::string progressString() {
        std::ostringstream msg;
        if (it_ == numIt_) {
            msg << "100% (done)                         \n";
        } else {
            double pct = std::round(it_ * 100.0 / numIt_);
            msg << pct << "%  (~" << formatTime(remainingSecs()) << " remaining)       ";
        }
        return msg.str();
    }

    //! formats time into {days}d{hours}h{minutes}m{seconds}s.
    //! @param secs in seconds.
    std::string formatTime(size_t secs) {
        std::ostringstream msg;
        constexpr size_t minute = 60;
        constexpr size_t hour = 60 * minute;
        constexpr size_t day = 24 * hour;
        int numUnits = 0;
        if (secs / day > 0) {
            msg << secs / day << "d";
            secs = secs % day;
            numUnits++;
        }
        if (secs / hour > 0) {
            msg << secs / hour << "h";
            secs = secs % hour;
            numUnits++;
        }
        if ((secs / minute > 0) && (numUnits < 2)) {
            msg << secs / minute << "m";
            secs = secs % minute;
            numUnits++;
        }
        if (numUnits < 2)
            msg << secs << "s";
        return msg.str();
    }

    std::atomic_size_t it_{0};          //!< iteration counter
    std::atomic_size_t numUpdates_{0};  //!< counter for the number of updates
    std::atomic_bool isDone_{false};    //!< flag indicating end of iterations
    size_t numIt_;                      //!< total number of iterations
    size_t printEvery_;                 //!< update frequency
    //! time at start
    std::chrono::time_point<std::chrono::steady_clock> startTime_;
};


//! @brief A counter showing progress in percent.
//!
//! Prints to the R console in a thread safe manner using `Rcout`
//! (an instance of `RcppThread::Rprinter`).
class ProgressCounter : public ProgressPrinter {

public:
    //! constructs a progress counter.
    //! @param numIt total number of iterations.
    //! @param printEvery how regularly to print updates (in seconds).
    //! @param custom_msg (optional) a custom message to be printed before the
    //!   progress count; default is `"Computing: "`.
    ProgressCounter(size_t numIt, size_t printEvery, std::string custom_msg = "Computing: ") :
        ProgressPrinter(numIt, printEvery), cmsg(custom_msg)
    {}

private:
    std::string cmsg;
    //! prints progress in percent to the R console (+ an estimate of remaining
    //! time).
    void printProgress() {
        if (isDone_)
            return;
        if (it_ == numIt_)
            isDone_ = true;
        std::ostringstream msg;
        msg << "\r" << cmsg << progressString();
        Rcout << msg.str();
    }
};

//! @brief A progress bar showing progress in percent.
//!
//! Prints to the R console in a thread safe manner using `Rcout`
//! (an instance of `RcppThread::Rprinter`).
class ProgressBar : public ProgressPrinter {

public:
    //! constructs a progress bar.
    //! @param numIt total number of iterations.
    //! @param printEvery how regularly to print updates (in seconds).
    ProgressBar(size_t numIt, size_t printEvery) :
        ProgressPrinter(numIt, printEvery)
    {}

private:
    //! prints a progress bar to the R console (+ an estimate of remaining
    //! time).
    void printProgress() {
        if (isDone_)
            return;
        if (it_ == numIt_)
            isDone_ = true;
        double pct = std::round(it_ * 100.0 / numIt_);
        std::ostringstream msg;
        msg << "\rComputing: " << makeBar(pct) << progressString();
        Rcout << msg.str();
    }

    //! constructs the progress bar.
    //! @param pct progress in percent.
    //! @param numBars bar is split into `numBars` units.
    std::string makeBar(size_t pct, size_t numBars = 40) {
        std::ostringstream msg;
        msg << "[";
        size_t i = 0;
        for (; i < pct / 100.0 * numBars; i++)
            msg << "=";
        for (; i < numBars; i++)
            msg << " ";
        msg << "] ";
        return msg.str();
    }
};


}
