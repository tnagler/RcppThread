# RcppThread 1.1.0

* Add R function `detectCores()` (#48).

* Add classes `ProgressCounter` and `ProgressBar` for tracking progress in long-
  running loops (#49).

* Increased speed for short running tasks due to lock-free queue (#50).


# RcppThread 1.0.0

* Release for JSS publication https://doi.org/10.18637/jss.v097.c01.


# RcppThread 0.5.4

* Fixed warning for move constructor in ThreadPool (#35, #36, thanks @asardaes 
  for noticing).


# RcppThread 0.5.3

* Improved handling of exceptions thrown from threads.


# RcppThread 0.5.2

* Limit number of threads in unit tests.

* Fixed typos in package vignette.


# RcppThread 0.5.1

BUG FIXES

* Fix portability issues related to `native_handle_type`.

* Fix signed/unsigned comparison in `parallelFor()`.

* Fix signed/unsigned warnings in unit tests.


# RcppThread 0.5.0

DEPENDENCIES

* Rcpp is no longer a hard dependency, but only used for unit tests. This avoids unnecessary compilation time during package installation. 

NEW FEATURES

* New vignette available, see `browseVignettes("RcppThread")`.

* New functions `parallelFor()` and `ForEach()` allowing parallel `for` loops 
  with load balancing. Can also be called as method of a `ThreadPool`.

* Options to override `std::thread` and `std::cout` with RcppThread equivalents 
  using preprocessor variables `RCPPTHREAD_OVERRIDE_THREAD` and 
  `RCPPTHREAD_OVERRIDE_COUT`.
  
* Several minor performance optimizations.


# RcppThread 0.4.0

NEW FEATURE

* New function `ThreadPool::map()` that allows to map a function a list of items.


# RcppThread 0.3.0

NEW FEATURE

* A `ThreadPool` can now be instantiated with zero threads in the pool. It
  will then do all work pushed to it in the main thread.


# RcppThread 0.2.0

NEW FEATURE

* `ThreadPool` has a new method `wait()` that waits for all jobs to be done 
  without joining the threads. This way the thread pool can be re-used for 
  different types of tasks that need to be run sequentially.


# RcppThread 0.1.3

BUG FIX

* Don't check print output of multi-threaded code b/c of random results.


# RcppThread 0.1.2

DEPENDS

* Now available for `R (>= 3.3.0)`.

BUG FIX

* Fixed a randomly failing unit test.


# RcppThread 0.1.1

BUG FIX

* Default initialize static `Rcout` instance in header file (#9; couldn't link
  shared library on r-hub, see discussion in #8) 


# RcppThread 0.1.0

* Initial release.
