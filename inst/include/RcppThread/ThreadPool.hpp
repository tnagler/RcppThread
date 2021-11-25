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

    ~ThreadPool() noexcept;

    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;

    template<class F, class... Args>
    void push(F&& f, Args&&... args);

    template<class F, class... Args>
    auto pushReturn(F&& f, Args&&... args) -> std::future<decltype(f(args...))>;

    template<class F, class I>
    void map(F&& f, I&& items);

    template<class F>
    inline void parallelFor(int begin, size_t end, F&& f, size_t nBatches = 0);

    template<class F, class I>
    inline void parallelForEach(I& items, F&& f, size_t nBatches = 0);

    void wait();
    void join();

  private:
    // variables for synchronization between workers (destructed last)
    const size_t nWorkers_;
    struct Sync
    {
        explicit Sync(size_t nWorkers)
          : taskManager_{ nWorkers }
        {}
        quickpool::detail::TaskManager taskManager_;
        quickpool::TodoList todoList_{ 0 };
        quickpool::TodoList running_{ 0 };
    };
    std::shared_ptr<Sync> sync_;
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
  , sync_{ std::make_shared<Sync>(nWorkers) }
{
    auto sync = sync_;
    for (size_t id = 0; id < nWorkers; id++) {
        std::thread([sync, id] {
            sync->running_.add();

            std::function<void()> task;
            while (!sync->taskManager_.stopped()) {

                sync->taskManager_.wait_for_jobs(id);
                do {
                    while (sync->taskManager_.try_pop(task, id)) {
                        try {
                            task();
                            sync->todoList_.cross();
                        } catch (...) {
                            sync->todoList_.stop(std::current_exception());
                            sync->taskManager_.stop();
                        }
                    }
                } while (!sync->todoList_.empty() &&
                         !sync->taskManager_.stopped());
            }

            sync->running_.cross();
        }).detach();
    }
}

//! destructor joins all threads if possible.
inline ThreadPool::~ThreadPool() noexcept
{
    sync_->taskManager_.stop();
}

//! pushes jobs to the thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//!
//! The function returns void; if a job returns a result, use `pushReturn()`.
template<class F, class... Args>
void
ThreadPool::push(F&& f, Args&&... args)
{
    if (nWorkers_ == 0) {
        f(args...); // if there are no workers, do the job in the main thread
    } else {
        sync_->todoList_.add();
        sync_->taskManager_.push(
          std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }
}

//! pushes jobs returning a value to the thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//! @return an `std::shared_future`, where the user can get the result and
//!   rethrow the catched exceptions.
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
ThreadPool::map(F&& f, I&& items)
{
    auto pushJob = [=] {
        for (auto&& item : items)
            this->push(f, item);
    };
    this->push(pushJob);
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
    auto doBatch = [f](const Batch& b) {
        for (int i = b.begin; i < b.end; i++)
            f(i);
    };
    auto batches = createBatches(begin, size, nWorkers_, nBatches);
    auto pushJob = [=] {
        for (const auto& batch : batches)
            this->push(doBatch, batch);
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

//! waits for all jobs to finish and checks for interruptions,
//! but does not join the threads.
inline void
ThreadPool::wait()
{
    while (!sync_->todoList_.empty()) {
        sync_->todoList_.wait(50);
        Rcout << "";
        checkUserInterrupt();
    }
    Rcout << "";
}

//! waits for all jobs to finish and joins all threads.
inline void
ThreadPool::join()
{
    this->wait();
    sync_->taskManager_.stop();
    sync_->running_.wait();
}

namespace global {
static ThreadPool pool{ std::thread::hardware_concurrency() };
}

}
