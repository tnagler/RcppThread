// Copyright © 2022 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include <ostream>
#include "RcppThread/RMonitor.hpp"

namespace RcppThread {

//! Safely printing to the R console from threaded code.
class RErrPrinter {
public:

    //! prints `object` to R error stream íf called from main thread; otherwise
    //! adds a printable version of `object` to a buffer for deferred printing.
    //! @param object a string (or coercible object) to print.
    //! @details Declared as a friend in `RMonitor`.
    template<class T>
    RErrPrinter& operator<< (T& object)
    {
        RMonitor::instance().safelyPrintErr(object);
        return *this;
    }

    //! prints `object` to R error stream íf called from main thread; otherwise
    //! adds a printable version of `object` to a buffer for deferred printing.
    //! @param object a string (or coercible object) to print.
    //! @details Declared as a friend in `RMonitor`.
    template<class T>
    RErrPrinter& operator<< (const T& object)
    {
        RMonitor::instance().safelyPrintErr(object);
        return *this;
    }

    //! prints `object` to R error stream íf called from main thread; otherwise
    //! adds a printable version of `object` to a buffer for deferred printing.
    //! @param object a string (or coercible object) to print.
    //! @details Declared as a friend in `RMonitor`.
    RErrPrinter& operator<< (std::ostream& (*object)(std::ostream&))
    {
        RMonitor::instance().safelyPrintErr(object);
        return *this;
    }
};

//! global `RPrinter` instance called 'Rcerr' (as in Rcpp).
static RErrPrinter Rcerr = RErrPrinter();

}

// override std::cout to use RcppThread::Rcout instead
#ifndef RCPPTHREAD_OVERRIDE_CERR
    #define RCPPTHREAD_OVERRIDE_CERR 0
#endif

#if RCPPTHREAD_OVERRIDE_CERR
    #define cerr RcppThreadRcerr
    namespace std {
        static RcppThread::RErrPrinter RcppThreadRcerr = RcppThread::RErrPrinter();
    }
#endif
