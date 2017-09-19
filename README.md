# RcppThread

Provides R-friendly threading functionality: 

  * `Thread`: an interruptible thread class that otherwise behaves like 
   [`std::thread`](http://en.cppreference.com/w/cpp/thread/thread).
  * `ThreadPool`: a `Thread`-based class implementing the [thread pool
    pattern](https://en.wikipedia.org/wiki/Thread_pool) for easy and flexible
    parallelism.
  * thread safe versions of [Rcpp's](http://www.rcpp.org/)
    `checkUserInterrupt()` and `Rcout`.

The library is header-only, platform-independent, and only 
requires a 
[C++11-compatible compiler](http://en.cppreference.com/w/cpp/compiler_support#cpp11).

## Functionality

For a detailed description of its functionality and examples, see the 
[vignette](https://github.com/tnagler/RcppThread/blob/master/vignettes/RcppThread.Rmd).

## Installation

You can install RcppThread from github with:

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
2. Add `RcppThread` to the `LinkinTo` filed of your `DESCRIPTION` file.
