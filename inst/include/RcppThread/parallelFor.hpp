// Copyright © 2022 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include "RcppThread/ThreadPool.hpp"
#include <algorithm>

namespace RcppThread {

//! computes an index-based for loop in parallel batches.
//! @param begin first index of the loop.
//! @param end the loop runs in the range `[begin, end)`.
//! @param f a function (the 'loop body').
//! @param nThreads limits the number of threads used from the global pool.
//! @param nBatches the number of batches to create; the default (0)
//!   uses work stealing to distribute tasks.
//! @details Consider the following code:
//! ```
//! std::vector<double> x(10);
//! for (size_t i = 0; i < x.size(); i++) {
//!     x[i] = i;
//! }
//! ```
//! The parallel equivalent is
//! ```
//! parallelFor(0, 10, [&] (size_t i) {
//!     x[i] = i;
//! });
//! ```
//! The function dispatches to a global thread pool, so it can safely be nested
//! or called multiple times with almost no overhead.
//!
//! **Caution**: if the iterations are not independent from another,
//! the tasks need to be synchronized manually (e.g., using mutexes).
template<class F>
inline void
parallelFor(int begin,
            int end,
            F f,
            size_t nThreads = std::thread::hardware_concurrency(),
            size_t nBatches = 0)
{
    auto oldThreads = ThreadPool::globalInstance().getNumThreads();
    ThreadPool::globalInstance().setNumThreads(nThreads); 

    ThreadPool::globalInstance().parallelFor(begin, end, f, nBatches);
    ThreadPool::globalInstance().wait();

    ThreadPool::globalInstance().setNumThreads(oldThreads);
}

//! computes a range-based for loop in parallel batches.
//! @param items an object allowing for `items.size()` and whose elements
//!   are accessed by the `[]` operator.
//! @param f a function (the 'loop body').
//! @param nThreads limits the number of threads used from the global pool.
//! @param nBatches the number of batches to create; the default (0)
//!   uses work stealing to distribute tasks.
//! @details Consider the following code:
//! ```
//! std::vector<double> x(10, 1.0);
//! for (auto& xx : x) {
//!     xx *= 2;
//! }
//! ```
//! The parallel equivalent is
//! ```
//! parallelFor(x, [&] (double& xx) {
//!     xx *= 2;
//! });
//! ```
//! The function dispatches to a global thread pool, so it can safely be nested
//! or called multiple times with almost no overhead.
//!
//! **Caution**: if the iterations are not independent from another,
//! the tasks need to be synchronized manually (e.g., using mutexes).
template<class I, class F>
inline void
parallelForEach(I& items,
                F f,
                size_t nThreads = std::thread::hardware_concurrency(),
                size_t nBatches = 0)
{
    // loop ranges ranges indicate iterator offset
    auto begin = std::begin(items);
    auto size = std::distance(begin, std::end(items));
    parallelFor(
      0, size, [f, begin](int i) { f(*(begin + i)); }, nThreads, nBatches);
}

//! pushes jobs to the global thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//!
//! The function returns void; if a job returns a result, use
//! `async()`.
template<class F, class... Args>
inline void
push(F&& f, Args&&... args)
{
    ThreadPool::globalInstance().push(std::forward<F>(f),
                                      std::forward<Args>(args)...);
}

//! pushes jobs returning a value to the global thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//! @return an `std::shared_future`, where the user can get the result and
//!   rethrow exceptions.
template<class F, class... Args>
inline auto
pushReturn(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
{
    return ThreadPool::globalInstance().pushReturn(std::forward<F>(f),
                                                   std::forward<Args>(args)...);
}

//! pushes jobs returning a value to the global thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//! @return an `std::shared_future`, where the user can get the result and
//!   rethrow exceptions.
template<class F, class... Args>
inline auto
async(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
{
    return pushReturn(std::forward<F>(f), std::forward<Args>(args)...);
}

//! waits for all jobs to finish and checks for interruptions, but only from the
//! main thread. Does nothing when called from other threads.
inline void
wait()
{
    ThreadPool::globalInstance().wait();
}

}
