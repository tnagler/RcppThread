# RcppThread

<!-- badges: start -->
[![R build status](https://github.com/tnagler/RcppThread/workflows/R-CMD-check/badge.svg)](https://github.com/tnagler/RcppThread/actions)
[![CRAN version](https://www.r-pkg.org/badges/version/RcppThread)](https://cran.r-project.org/package=RcppThread) 
[![CRAN downloads](https://cranlogs.r-pkg.org/badges/RcppThread)](https://cran.r-project.org/package=RcppThread)
<!-- badges: end -->

Provides R-friendly threading functionality: 

  * thread safe versions of [Rcpp's](https://www.rcpp.org/)
    `checkUserInterrupt()`, `Rcout`, and `Rcerr`,
  * an interruptible thread class that otherwise behaves like 
   [`std::thread`](https://en.cppreference.com/w/cpp/thread/thread),
  * classes for the [thread pool
    pattern](https://en.wikipedia.org/wiki/Thread_pool) and parallel for loops
    for easy and flexible parallelism,
  * thread safe progress tracking,
  * state-of-the art speed, see  [benchmarks](https://github.com/tnagler/RcppThread/blob/main/benchmarks/benchmarks.md).

The library is header-only, platform-independent, and only 
requires a 
[C++11-compatible compiler](https://en.cppreference.com/w/cpp/compiler_support#cpp11).

## Functionality

For a detailed description of its functionality and examples, see the associated
[JSS paper](https://doi.org/10.18637/jss.v097.c01)
or the [API documentation](https://tnagler.github.io/RcppThread/).

Since then, the following **new features** have been added:

- Printing to the error stream with `Rcerr`.

- Free-standing functions like `parallelFor()` now dispatch 
  to a global thread pool that persists for the entire session. This 
  significantly speeds up programs that repeatedly call these functions.

- Faster runtimes due to lock-free work stealing queue and loops (from [quickpool](https://github.com/tnagler/quickpool)).

- Option to resize a thread pool.

- An R function `RcppThread::detectCores()` to determine the number of (logical)
  cores on your machine.

- C++ classes `ProgressCounter` and `ProgressBar` for tracking progress in 
  long-running loops.  
  **Example usage:**
  ``` cpp
    // 20 iterations in loop, update progress every 1 sec
    RcppThread::ProgressBar bar(20, 1);
    RcppThread::parallelFor(0, 20, [&] (int i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        bar++;
    });
  ```
  **Output:** (just one line that is continuously updated)
  ``` 
  ...
  Computing: [==========================              ] 65% (~1s remaining)       
  ...
  Computing: [========================================] 100% (done) 
  ```

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

###### with cppFunction

Pass `"RcppThread"` to the `depends` argument and `"cpp11"` to the `plugins`
argument. For example:
``` r
Rcpp::cppFunction('void func() { /* actual code here */ }', 
                       depends = "RcppThread", plugins = "cpp11")
```

###### with sourceCpp

Add 
``` cpp
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::depends(RcppThread)]]
```
before including any headers in your source code.

###### in another R package

1. Add the line `CXX_STD = CXX11` to the `src/Makevars(.win)` files of your package.
2. Add `RcppThread` to the `LinkingTo` field of your `DESCRIPTION` file.

For optimal portability, you might also want to add
``` 
PKG_LIBS = `"$(R_HOME)/bin/Rscript" -e "RcppThread::LdFlags()"`
```
to your `src/Makevars` (not `.win`). This adds `-latomic`/`-lpthread` flags as 
necessary and available.

## Automatic override of `std::cout`, `std::cerr`, and `std::thread`

There are preprocessor options to replace all occurrences of `std::cout`, `std::cerr`, and `std::thread` with calls to `RcppThread::Rcout`, `RcppThread::Rcerr`, and `RcppThread::Thread`
(provided that the RcppThread headers are included first). To enable this, use 

```
#define RCPPTHREAD_OVERRIDE_COUT 1    // std::cout override
#define RCPPTHREAD_OVERRIDE_CERR 1    // std::cerr override
#define RCPPTHREAD_OVERRIDE_THREAD 1  // std::thread override
```
before including the RcppThread headers.


## References

Nagler, T. (2021). "R-Friendly Multi-Threading in C++." _Journal of Statistical
Software, Code Snippets_, *97*(1), 1-18. [doi: 10.18637/jss.v097.c01](https://doi.org/10.18637/jss.v097.c01)

