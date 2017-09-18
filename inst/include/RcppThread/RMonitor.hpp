// Copyright © 2017 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

// R API
#include <Rcpp.h>

// for tracking threads
#include <thread>
#include <mutex>
#include <atomic>
#include <stdexcept>

namespace RcppThread {

//! global variable holding id of master thread-
static std::thread::id masterThreadID = std::this_thread::get_id();

//! A singleton class for tracking threads and safe communication.
class RMonitor {
    // user-facing functionality must be friends, so they can access
    // protected members of RMonitor.
    friend class RPrinter;
    friend void checkUserInterrupt(bool condition = true);
    friend bool isInterrupted(bool condition = true);

public:
    RMonitor(RMonitor const&) = delete;            // copy construct
    RMonitor(RMonitor&&) = delete;                 // move construct
    RMonitor& operator=(RMonitor const&) = delete; // copy assign
    RMonitor& operator=(RMonitor &&) = delete;     // move assign

    //! constructs the instance when called for the first time, returns ref
    //! to instance.
    static RMonitor& instance()
    {
        static RMonitor instance_;
        return instance_;
    }


protected:
    //! returns `true` only when called from master thread.
    bool iAmMaster()
    {
        return (masterThreadID == std::this_thread::get_id());
    }

    //! checks for user interruptions, but only if called from master thread.
    void safelycheckUserInterrupt()
    {
        if ( safelyIsInterrupted() ) {
            throw std::runtime_error("C++ call interrupted by user");
        }
    }

    //! checks for user interruptions, but only if called from master thread.
    bool safelyIsInterrupted()
    {
        if ( iAmMaster() ) {
            isInterrupted_ = isInterrupted();
        }
        return isInterrupted_;
    }

    //! prints `object` to R console íf called from master thread; otherwise
    //! adds a printable version of `object` to a buffer for deferred printing.
    //! @param object a string to print.
    template<class T>
    void safelyPrint(const T& object)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            msgs_ << object;
        }
        safelyReleaseMsgBuffer();
    }

    //! prints the messages in the buffer to the R console, but only if called
    //! from master thread.
    void safelyReleaseMsgBuffer()
    {
        std::lock_guard<std::mutex> lk(m_);
        if ( iAmMaster() ) {
            msgs_.seekg(0, std::ios::end);
            if ( msgs_.tellg() ) {
                // release messages in buffer
                Rcpp::Rcout << msgs_.str();
                // clear message buffer
                msgs_ = std::stringstream();
            }
        }
    }

private:
    //! Ctors declared private, to instantiate class use `::instance()`.
    RMonitor(void) : isInterrupted_(false) {}

    //! calls R's API to check for user interrupt.
    //!
    //! Since the R API is not thread safe, it must be called in top-level
    //! context to avoid long-jumping out of the current context.
    static void callRCheck(void *dummy) {
        R_CheckUserInterrupt();
    }

    //! checks for R user interruptions in top level context.
    inline bool isInterrupted() {
        return (R_ToplevelExec(callRCheck, NULL) == FALSE);
    }

    std::mutex m_;                           // mutex for synchronized r/w
    std::stringstream msgs_;                 // string buffer
    std::atomic_bool isInterrupted_;
};

// user-facing API -----------------------------

//! checks for user interruptions, but only if called from master thread.
//! @param condition optional; a condition for the check to be executed.
//! @details Declared as a friend in `RMonitor`.
inline void checkUserInterrupt(bool condition)
{
    if (condition)
        RMonitor::instance().safelycheckUserInterrupt();
}

//! checks for user interruptions, but only if called from master thread
//! (otherwise returns `false`).
//! @param condition optional; a condition for the check to be executed.
//! @details Declared as a friend in `RMonitor`.
inline bool isInterrupted(bool condition)
{
    if (condition)
        return RMonitor::instance().safelyIsInterrupted();
    return false;
}

}
