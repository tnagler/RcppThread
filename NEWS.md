# RcppThread 0.4.0 (in progress)

NEW FEATURE

* New function `ThreadPool::map()` that allows to map a function a list of items.


# RcppThread 0.3.0 (May 22, 2018)

NEW FEATURE

* A `ThreadPool` can now be intantiated with zero threads in the pool. It
  will then do all work pushed to it in the main thread.


# RcppThread 0.2.0 (April 23, 2018)

NEW FEATURE

* `ThreadPool` has a new method `wait()` that waits for all jobs to be done 
  without joining the threads. This way the thread pool can be re-used for 
  different types of tasks that need to be run sequentially.


# RcppThread 0.1.3 (November 27, 2017)

BUG FIX

* Don't check print ouptut of multi-threaded code b/c of random results.


# RcppThread 0.1.2 (November 24, 2017)

DEPENDS

* Now available for `R (>= 3.3.0)`.

BUG FIX

* Fixed a randomly failing unit test.


# RcppThread 0.1.1 (November 7, 2017)

BUG FIX

* Default initialize static `Rcout` instance in header file (#9; couldn't link
  shared library on r-hub, see discussion in #8) 


# RcppThread 0.1.0 (October 27, 2017)

* Intial release.



