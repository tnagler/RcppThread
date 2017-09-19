// Copyright © 2017 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include <ostream>
#include "RcppThread/RMonitor.hpp"

namespace RcppThread {

//! Safely printing to the R console from threaded code.
class RPrinter {
public:
    
    //! @defgroup formatting Formatting operator
    //! prints `object` to R console íf called from master thread; otherwise
    //! adds a printable version of `object` to a buffer for deferred printing.
    //! @param object a string (or coercible object) to print.
    //! @details Declared as a friend in `RMonitor`.
    //! @{
    
    //! by reference.
    template<class T>
    RPrinter& operator << (T& object)
    {
        RMonitor::instance().safelyPrint(object);
        return *this;
    }
    
    //! by constant reference.
    template<class T>
    RPrinter& operator << (const T& object)
    {
        RMonitor::instance().safelyPrint(object);
        return *this;
    }
    
    //! handles std::endl.
    RPrinter& operator<< (std::ostream& (*object)(std::ostream&))
    {
        RMonitor::instance().safelyPrint(object);
        return *this;
    }
    //! @}
};

//! global `RPrinter` instance called 'Rcout' (as in Rcpp).
static RPrinter Rcout;

}
