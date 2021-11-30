// Copyright © 2021 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include "RcppThread/Batch.hpp"
#include "RcppThread/RMonitor.hpp"
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

    ~ThreadPool();

    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;

    static ThreadPool& globalInstance();

    template<class F, class... Args>
    void push(F&& f, Args&&... args);

    template<class F, class... Args>
    auto pushReturn(F&& f, Args&&... args) -> std::future<decltype(f(args...))>;

    template<class F, class I>
    void map(F f, I& items);

    template<class F>
    void parallelFor(int begin, size_t end, F&& f, size_t nBatches = 0);

    template<class F, class I>
    void parallelForEach(I& items, F&& f, size_t nBatches = 0);

    void wait();
    void join();

  private:
    // variables for synchronization between workers (destructed last)
    const size_t nWorkers_;
    std::vector<std::thread> workers_;
    quickpool::detail::TaskManager taskManager_;
};

//! constructs a thread pool with as many workers as there are cores.
inline ThreadPool::ThreadPool()
  : ThreadPool(std::thread::hardware_concurrency())
{}

//! constructs a thread pool with `nWorkers` threads.
//! @param nWorkers number of worker threads to create; if `nWorkers = 0`, all
//!    work pushed to the pool will be done in the main thread.
inline ThreadPool::ThreadPool(size_t nWorkers)
  : nWorkers_{ nWorkers }
  , taskManager_{ nWorkers }
{
    workers_.reserve(nWorkers);
    for (size_t id = 0; id < nWorkers; id++) {
        workers_.emplace_back([this, id] {
            quickpool::detail::Task task;
            while (!taskManager_.stopped()) {
                taskManager_.wait_for_jobs(id);
                do {
                    while (taskManager_.try_pop(task, id)) {
                        try {
                            task();
                            taskManager_.report_success();
                        } catch (...) {
                            taskManager_.report_fail(std::current_exception());
                        }
                    }
                } while (!taskManager_.done());
            }
        });
    }
}

//! destructor joins all threads if possible.
inline ThreadPool::~ThreadPool()
{
    taskManager_.stop();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

//! Access to the global thread pool instance.
inline ThreadPool& ThreadPool::globalInstance()
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
    if (nWorkers_ == 0) {
        f(args...); // if there are no workers, do job in main thread
    } else {
        taskManager_.push(
          std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }
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
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    using pack_t = std::packaged_task<decltype(f(args...))()>;
    auto ptr = std::make_shared<pack_t>(std::move(task));
    this->push([ptr] { (*ptr)(); });
    return ptr->get_future();
}

//! maps a function on a list of items, possibly running tasks in parallel.
//! @param f function to be mapped.
//! @param items an objects containing the items on which `f` shall be
//!   mapped; must allow for `auto` loops (i.e., `std::begin(I)`/
//!  `std::end(I)` must be defined).
template<class F, class I>
void
ThreadPool::map(F f, I& items)
{
    auto pushJob = [&] {
        for (auto& item : items)
            this->push(f, item);
    };
    this->pushReturn(pushJob).wait();
}

//! computes an index-based for loop in parallel batches.
//! @param begin first index of the loop.
//! @param size the loop runs in the range `[begin, begin + size)`.
//! @param f an object callable as a function (the 'loop body'); typically
//!   a lambda.
//! @param nBatches the number of batches to create; the default (0)
//!   triggers a heuristic to automatically determine the batch size.
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
ThreadPool::parallelFor(int begin, size_t size, F&& f, size_t nBatches)
{
    if (size == 0)
        return;
    auto ff = std::bind(f, std::placeholders::_1);
    const auto doBatch = [ff](Batch b) {
        for (int i = b.begin; i < b.end; i++)
            ff(i);
    };
    auto pushJob = [=] {
        auto batches = createBatches(begin, size, workers_.size(), nBatches);
        for (const auto& batch : batches) {
            this->push(doBatch, batch);
        }
    };
    this->push(pushJob);
}

//! computes a for-each loop in parallel batches.
//! @param items an object allowing for `std::begin()`/`std::end()` and
//!   whose elements can be accessed by the `[]` operator.
//! @param f a function (the 'loop body').
//! @param nBatches the number of batches to create; the default (0)
//!   triggers a heuristic to automatically determine the number of batches.
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
ThreadPool::parallelForEach(I& items, F&& f, size_t nBatches)
{
    this->parallelFor(0, items.size(), [&items, f](size_t i) { f(items[i]); });
}

//! waits for all jobs to finish and checks for interruptions, but only from the
//! thread that created the pool.Does nothing when called from other threads.
inline void
ThreadPool::wait()
{
    if (!taskManager_.called_from_owner_thread()) {
        // Only the thread that constructed the pool can wait.
        return;
    }

    while (!taskManager_.done()) {
        taskManager_.wait_for_finish(100);
        Rcout << "";
        checkUserInterrupt();
    }

    // release potentially pending msgs and exception
    Rcout << "";
    taskManager_.rethrow_exception();
}

//! waits for all jobs to finish and joins all threads.
inline void
ThreadPool::join()
{
    this->wait();
    taskManager_.stop();
    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
}

} // end namespace RcppThread
