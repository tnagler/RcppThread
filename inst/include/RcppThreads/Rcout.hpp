// Copyright © 2017 Thomas Nagler
//
// This file is part of the RcppThreads and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThreads or https://github.com/tnagler/RcppThreads/blob/master/LICENSE.md.

#pragma once

#include <ostream>
#include "RcppThreads/RMonitor.hpp"

namespace RcppThreads {

//! A class for safely printing to the R console from threaded code.
class RPrinter {
public:

    //! prints `object` to R console íf called from master thread; otherwise
    //! adds a printable version of `object` to a buffer for deferred printing.
    //! @param object a string (or coercible object) to print.
    //! @details Declared as a friend in `RMonitor`.
    template<class T>
    RPrinter& operator << (T& object)
    {
        RMonitor::instance().safelyPrint(object);
        return *this;
    }

    template<class T>
    RPrinter& operator << (const T& object)
    {
        RMonitor::instance().safelyPrint(object);
        return *this;
    }

    //! prints the messages in the buffer to the R console, but only if called
    //! from master thread.
    void release()
    {
        RMonitor::instance().safelyReleaseMsgBuffer();
    }
};

//! global instance called 'Rcout' (as in Rcpp)
static RPrinter Rcout;

}
