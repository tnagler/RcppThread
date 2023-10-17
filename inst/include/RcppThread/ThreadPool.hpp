// Copyright © 2022 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include "RcppThread/RMonitor.hpp"
#include "RcppThread/Rcerr.hpp"
#include "RcppThread/Rcout.hpp"
#include "RcppThread/quickpool.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace RcppThread {

//! Implemenation of the thread pool pattern based on `Thread`.
class ThreadPool
{
  public:
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool();
    explicit ThreadPool(size_t nWorkers);

    ~ThreadPool() noexcept;

    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;

    //! Access to the global thread pool instance.
    static ThreadPool& globalInstance();

    void resize(size_t threads);

    template<class F, class... Args>
    void push(F&& f, Args&&... args);

    template<class F, class... Args>
    auto pushReturn(F&& f, Args&&... args) -> std::future<decltype(f(args...))>;

    template<class F, class I>
    void map(F&& f, I&& items);

    template<class F>
    void parallelFor(int begin, int end, F f, size_t nBatches = 0);

    template<class F, class I>
    void parallelForEach(I& items, F f, size_t nBatches = 0);

    void wait();
    void join();

    void setNumThreads(size_t threads);
    size_t getNumThreads() const;

  private:
    std::unique_ptr<quickpool::ThreadPool> pool_;
    std::thread::id owner_thread_;
};

//! constructs a thread pool with as many workers as there are cores.
inline ThreadPool::ThreadPool()
  : ThreadPool(std::thread::hardware_concurrency())
{
}

//! constructs a thread pool with `nWorkers` threads.
//! @param nWorkers number of worker threads to create; if `nWorkers = 0`, all
//!    work pushed to the pool will be done in the main thread.
inline ThreadPool::ThreadPool(size_t nWorkers)
  : pool_{ new quickpool::ThreadPool(nWorkers) }
  , owner_thread_{ std::this_thread::get_id() }
{
}

//! destructor joins all threads if possible.
inline ThreadPool::~ThreadPool() noexcept {}

//! Access to the global thread pool instance.
inline ThreadPool&
ThreadPool::globalInstance()
{
#ifdef _WIN32
    // Must leak resource, because windows + R deadlock otherwise. Memory
    // is released on shutdown.
    static auto ptr = new ThreadPool;
    return *ptr;
#else
    static ThreadPool instance_;
    return instance_;
#endif
}

//! changes the number of threads in the pool.
//! @param num_threads the new number of threads.
inline void
ThreadPool::resize(size_t num_threads)
{
    pool_->set_active_threads(num_threads);
}

//! pushes jobs to the thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//!
//! The function returns void; if a job returns a result, use
//! `pushReturn()`.
template<class F, class... Args>
void
ThreadPool::push(F&& f, Args&&... args)
{
    pool_->push(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
}

//! pushes jobs returning a value to the thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//! @return an `std::shared_future`, where the user can get the result and
//!   rethrow exceptions.
template<class F, class... Args>
auto
ThreadPool::pushReturn(F&& f, Args&&... args)
  -> std::future<decltype(f(args...))>
{
    return pool_->async(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));
}

//! maps a function on a list of items, possibly running tasks in parallel.
//! @param f function to be mapped.
//! @param items an objects containing the items on which `f` shall be
//!   mapped; must allow for `auto` loops (i.e., `std::begin(I)`/
//!  `std::end(I)` must be defined).
template<class F, class I>
void
ThreadPool::map(F&& f, I&& items)
{
    for (auto&& item : items)
        this->push(std::forward<F>(f), item);
}

//! computes an index-based for loop in parallel batches.
//! @param begin first index of the loop.
//! @param end the loop runs in the range `[begin, end)`.
//! @param f an object callable as a function (the 'loop body'); typically
//!   a lambda.
//! @param nBatches the number of batches to create; the default (0)
//!   uses work stealing to distribute tasks.
//! @details Consider the following code:
//! ```
//! std::vector<double> x(10);
//! for (size_t i = 0; i < x.size(); i++) {
//!     x[i] = i;
//! }
//! ```
//! The parallel equivalent is given by:
//! ```
//! ThreadPool pool(2);
//! pool.forIndex(0, 10, [&] (size_t i) {
//!     x[i] = i;
//! });
//! ```
//! **Caution**: if the iterations are not independent from another,
//! the tasks need to be synchronized manually (e.g., using mutexes).
template<class F>
inline void
ThreadPool::parallelFor(int begin, int end, F f, size_t nBatches)
{
    if (nBatches == 0) {
        // each worker has its dedicated range, but can steal part of another
        // worker's ranges when done with own
        auto thr =
          std::max(pool_->get_active_threads(), static_cast<size_t>(1));
        auto workers = quickpool::loop::create_workers<F>(f, begin, end, thr);
        for (size_t k = 0; k < thr; k++) {
            this->push([=] { workers->at(k).run(workers); });
        }
    } else {
        // manual batching for backwards compatibility
        size_t nTasks = std::max(end - begin, static_cast<int>(0));
        if (nTasks <= 0)
            return;
        nBatches = std::min(nBatches, nTasks);

        size_t sz = nTasks / nBatches;
        int rem = nTasks % nBatches;
        for (size_t b = 0; b < nBatches; b++) {
            int bs = sz + (rem-- > 0);
            this->push([=] {
                for (int i = begin; i < begin + bs; ++i)
                    f(i);
            });
            begin += bs;
        }
    }
}

//! computes a for-each loop in parallel batches.
//! @param items an object allowing for `std::begin()`/`std::end()` and
//!   whose elements can be accessed by the `[]` operator.
//! @param f a function (the 'loop body').
//! @param nBatches the number of batches to create; the default (0)
//!   uses work stealing to distribute tasks.
//! @details Consider the following code:
//! ```
//! std::vector<double> x(10, 1.0);
//! for (auto& xx : x) {
//!     xx *= 2;
//! }
//! ```
//! The parallel `ThreadPool` equivalent is
//! ```
//! ThreadPool pool(2);
//! pool.parallelForEach(x, [&] (double& xx) {
//!     xx *= 2;
//! });
//! ```
//! **Caution**: if the iterations are not independent from another,
//! the tasks need to be synchronized manually (e.g., using mutexes).
template<class F, class I>
inline void
ThreadPool::parallelForEach(I& items, F f, size_t nBatches)
{
    this->parallelFor(0, items.size(), [&items, f](size_t i) { f(items[i]); });
}

//! waits for all jobs to finish and checks for interruptions, but only from the
//! thread that created the pool.Does nothing when called from other threads.
inline void
ThreadPool::wait()
{
    if (std::this_thread::get_id() != owner_thread_) {
        // Only the thread that constructed the pool can wait.
        return;
    }
    do {
        pool_->wait(100);
        Rcout << "";
        Rcerr << "";
        checkUserInterrupt();

    } while (!pool_->done());
    Rcout << "";
    Rcerr << "";
}

//! waits for all jobs to finish.
inline void
ThreadPool::join()
{
    pool_->wait();
}

//! sets the number of active threads in the pool.
//! @param threads the desired number of threads.
inline void
ThreadPool::setNumThreads(size_t threads)
{
    pool_->set_active_threads(threads);
}

//! gets the number of active threads in the pool.
inline size_t
ThreadPool::getNumThreads() const
{
    return pool_->get_active_threads();
}

} // end namespace RcppThread
