// Copyright © 2021 Thomas Nagler
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
//! @param nThreads deprecated; loop is run on global thread pool.
//! @param nBatches the number of batches to create; the default (0)
//!   triggers a heuristic to automatically determine the number of batches.
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
//! The function sets up a `ThreadPool` object to do the scheduling. If you
//! want to run multiple parallel for loops, consider creating a `ThreadPool`
//! yourself and using `ThreadPool::parallelFor()`.
//!
//! **Caution**: if the iterations are not independent from another,
//! the tasks need to be synchronized manually (e.g., using mutexes).
template<class F>
inline void
parallelFor(int begin,
            int end,
            F&& f,
            size_t nThreads = std::thread::hardware_concurrency(),
            size_t nBatches = 0)
{
    if (end < begin)
        throw std::runtime_error("can only run forward loops");
    if (end == begin)
        return;

    ThreadPool pool(nThreads);
    pool.parallelFor(begin, end, std::forward<F>(f), nBatches);
    pool.join();
}

//! computes a range-based for loop in parallel batches.
//! @param items an object allowing for `items.size()` and whose elements
//!   are accessed by the `[]` operator.
//! @param f a function (the 'loop body').
//! @param nThreads deprecated; loop is run on global thread pool.
//! @param nBatches the number of batches to create; the default (0)
//!   triggers a heuristic to automatically determine the number of batches.
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
//! The function sets up a `ThreadPool` object to do the scheduling. If you
//! want to run multiple parallel for loops, consider creating a `ThreadPool`
//! yourself and using `ThreadPool::parallelForEach()`.
//!
//! **Caution**: if the iterations are not independent from another,
//! the tasks need to be synchronized manually (e.g., using mutexes).
template<class I, class F>
inline void
parallelForEach(I& items,
                F&& f,
                size_t nThreads = std::thread::hardware_concurrency(),
                size_t nBatches = 0)
{
    // loop ranges ranges indicate iterator offset
    ThreadPool pool(nThreads);
    pool.parallelForEach(items, std::forward<F>(f), nBatches);
    pool.join();
}

}
