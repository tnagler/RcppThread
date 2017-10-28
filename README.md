# RcppThread

[![Travis-CI Build Status](https://travis-ci.org/tnagler/RcppThread.svg?branch=master)](https://travis-ci.org/tnagler/RcppThread) 
[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/tnagler/RcppThread?branch=master&svg=true)](https://ci.appveyor.com/project/tnagler/RcppThread)
[![CRAN version](http://www.r-pkg.org/badges/version/RcppThread)](https://cran.r-project.org/package=RcppThread) 
[![Coverage Status](https://img.shields.io/codecov/c/github/tnagler/RcppThread/master.svg)](https://codecov.io/github/tnagler/RcppThread?branch=master)

Provides R-friendly threading functionality: 

  * thread safe versions of [Rcpp's](http://www.rcpp.org/)
    `checkUserInterrupt()` and `Rcout`,
  * an interruptible thread class that otherwise behaves like 
   [`std::thread`](http://en.cppreference.com/w/cpp/thread/thread),
  * a class implementing the [thread pool
    pattern](https://en.wikipedia.org/wiki/Thread_pool) for easy and flexible
    parallelism.

The library is header-only, platform-independent, and only 
requires a 
[C++11-compatible compiler](http://en.cppreference.com/w/cpp/compiler_support#cpp11).

## Functionality

For a detailed description of its functionality and examples, see the 
[vignette](https://github.com/tnagler/RcppThread/blob/master/vignettes/RcppThread.Rmd)
or the [API documentation](https://tnagler.github.io/RcppThread/).

## Installation

Release version from CRAN:

``` r
install.packages("RcppThread")
```

Latest development version from github:

``` r
# install.packages("devtools")
devtools::install_github("tnagler/RcppThread")
```

## How to use it

To use RcppThread, you need to tell the compiler that C++11 functionality will
be used:

###### with sourceCpp

1. Call `Sys.setenv(CXX_STD="CXX11")` in your R session before invoking `Rcpp::sourceCpp()`.

###### in another R package

1. Add the line `CXX_STD = CXX11` to the `src/Makevars(.win)` files of your package.
2. Add `RcppThread` to the `LinkingTo` filed of your `DESCRIPTION` file.
