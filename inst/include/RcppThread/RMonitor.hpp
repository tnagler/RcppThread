// Copyright © 2018 Thomas Nagler
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
#include <sstream>

namespace RcppThread {

//! global variable holding id of main thread.
static std::thread::id mainThreadID = std::this_thread::get_id();

class UserInterruptException : public std::exception {
    const char* what() const throw ()
    {
        return "C++ call interrupted by the user.";
    }
};

//! Singleton class for tracking threads and safe communication.
class RMonitor {
    // user-facing functionality must be friends, so they can access
    // protected members of RMonitor.
    friend class RPrinter;
    friend void checkUserInterrupt(bool condition);
    friend bool isInterrupted(bool condition);

public:
    //! copy constructor (forbidden)
    RMonitor(RMonitor const&) = delete;
    //! move constructor (forbidden)
    RMonitor(RMonitor&&) = delete;
    //! copy assignment (forbidden)
    RMonitor& operator=(RMonitor const&) = delete;
    //! move assignment (forbidden)
    RMonitor& operator=(RMonitor &&) = delete;

    //! constructs the instance when called for the first time, returns ref
    //! to instance.
    static RMonitor& instance()
    {
        static RMonitor instance_;
        return instance_;
    }


protected:
    //! returns `true` only when called from main thread.
    bool calledFromMainThread()
    {
        return (mainThreadID == std::this_thread::get_id());
    }

    //! checks for user interruptions, but only if called from main thread.
    void safelycheckUserInterrupt()
    {
        if ( safelyIsInterrupted() ) {
            if ( calledFromMainThread() )
                isInterrupted_ = false;  // reset for next call
            throw UserInterruptException();
        }
    }

    //! checks for user interruptions, but only if called from main thread.
    bool safelyIsInterrupted()
    {
        if (!isInterrupted_ &  calledFromMainThread())
            isInterrupted_ = isInterrupted();
        return isInterrupted_;
    }

    //! prints `object` to R console íf called from main thread; otherwise
    //! adds a printable version of `object` to a buffer for deferred printing.
    //! @param object a string to print.
    template<class T>
    void safelyPrint(const T& object)
    {
        std::lock_guard<std::mutex> lk(m_);
        msgs_ << object;
        if ( calledFromMainThread() ) {
            // release messages in buffer
            Rcpp::Rcout << msgs_.str();
            // clear message buffer
            msgs_.str("");
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


//! checks for user interruptions, but only if called from main thread.
//! @param condition optional; a condition for the check to be executed.
//! @details Declared as a friend in `RMonitor`.
inline void checkUserInterrupt(bool condition = true)
{
    if (condition)
        RMonitor::instance().safelycheckUserInterrupt();
}

//! checks for user interruptions, but only if called from main thread
//! (otherwise returns `false`).
//! @param condition optional; a condition for the check to be executed.
//! @details Declared as a friend in `RMonitor`.
inline bool isInterrupted(bool condition = true)
{
    if (condition)
        return RMonitor::instance().safelyIsInterrupted();
    return false;
}


}
