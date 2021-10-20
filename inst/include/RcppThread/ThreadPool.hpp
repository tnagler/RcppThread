// Copyright © 2021 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include "RcppThread/RMonitor.hpp"
#include "RcppThread/Rcout.hpp"
#include "RcppThread/Batch.hpp"
#include "RcppThread/TaskQueue.hpp"

#include <vector>
#include <thread>
#include <queue>
#include <condition_variable>
#include <future>      // std::shared_future
#include <mutex>
#include <atomic>
#include <memory>      // std::shared_ptr
#include <atomic>
#include <functional>  // std::function

namespace RcppThread {

//! Implemenation of the thread pool pattern based on `Thread`.
class ThreadPool {
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
    auto pushReturn(F&& f, Args&&... args)
        -> std::future<decltype(f(args...))>;

    template<class F, class I>
    void map(F&& f, I &&items);

    template<class F>
    inline void parallelFor(ptrdiff_t begin,
                            ptrdiff_t end,
                            F&& f,
                            size_t nBatches = 0);

    template<class F, class I>
    inline void parallelForEach(I& items, F&& f, size_t nBatches = 0);

    void wait();
    void join();
    void clear();

private:
    void startWorker();
    void doJob(std::function<void()>&& job);
    void announceBusy();
    void announceIdle();
    void announceStop();
    void joinWorkers();

    bool hasErrored();
    bool allJobsDone();
    bool waitForWakeUpEvent();
    void rethrowExceptions();

    std::vector<std::thread> workers_;
    TaskQueue jobs_{256};

    // variables for synchronization between workers
    std::mutex mTasks_;
    std::condition_variable cvTasks_;
    std::condition_variable cvBusy_;
    size_t numBusy_{0};
    bool stopped_{false};
    std::exception_ptr error_ptr_;
};

//! constructs a thread pool with as many workers as there are cores.
inline ThreadPool::ThreadPool() :
    ThreadPool(std::thread::hardware_concurrency())
{}

//! constructs a thread pool with `nWorkers` threads.
//! @param nWorkers number of worker threads to create; if `nWorkers = 0`, all
//!    work pushed to the pool will be done in the main thread.
inline ThreadPool::ThreadPool(size_t nWorkers)
{
  for (size_t w = 0; w < nWorkers; ++w)
    this->startWorker();
}


//! destructor joins all threads if possible.
inline ThreadPool::~ThreadPool() noexcept
{
    // destructors should never throw
    try {
        this->announceStop();
        this->joinWorkers();
    } catch (...) {}
}

//! pushes jobs to the thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//!
//! The function returns void; if a job returns a result, use `pushReturn()`.
template<class F, class... Args>
void ThreadPool::push(F&& f, Args&&... args)
{
    if (workers_.size() == 0) {
        f(args...); // if there are no workers, do the job in the main thread
        return;
    } else {
        // must hold lock while modifying the shared queue
        std::lock_guard<std::mutex> lk(mTasks_);
        if (stopped_)
            throw std::runtime_error("cannot push to joined thread pool");
        jobs_.push([f, args...] { f(args...); });
    }
    // signal a waiting worker that there's a new job
    cvTasks_.notify_one();
}

//! pushes jobs returning a value to the thread pool.
//! @param f a function taking an arbitrary number of arguments.
//! @param args a comma-seperated list of the other arguments that shall
//!   be passed to `f`.
//! @return an `std::shared_future`, where the user can get the result and
//!   rethrow the catched exceptions.
template<class F, class... Args>
auto ThreadPool::pushReturn(F&& f, Args&&... args)
    -> std::future<decltype(f(args...))>
{
    using jobPackage = std::packaged_task<decltype(f(args...))()>;
    auto job = std::make_shared<jobPackage>([&f, args...] {
        return f(args...);
    });
    this->push([job] { (*job)(); });

    return job->get_future();
}

//! maps a function on a list of items, possibly running tasks in parallel.
//! @param f function to be mapped.
//! @param items an objects containing the items on which `f` shall be
//!   mapped; must allow for `auto` loops (i.e., `std::begin(I)`/
//!  `std::end(I)` must be defined).
template<class F, class I>
void ThreadPool::map(F&& f, I &&items)
{
    for (auto &&item : items)
        this->push(f, item);
}

//! computes an index-based for loop in parallel batches.
//! @param begin first index of the loop.
//! @param end the loop runs in the range `[begin, end)`.
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
inline void ThreadPool::parallelFor(ptrdiff_t begin, ptrdiff_t end,
                                    F&& f,
                                    size_t nBatches)
{
    if (end < begin)
        throw std::range_error("end is less than begin; cannot run backward loops.");
    auto doBatch = [f] (const Batch& b) {
        for (ptrdiff_t i = b.begin; i < b.end; i++) f(i);
    };
    auto batches = createBatches(begin, end - begin, workers_.size(), nBatches);
    for (const auto& batch : batches)
        this->push(doBatch, batch);
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
inline void ThreadPool::parallelForEach(I& items, F&& f, size_t nBatches)
{
    auto doBatch = [f, &items] (const Batch& b) {
        for (ptrdiff_t i = b.begin; i < b.end; i++) f(items[i]);
    };
    size_t size = std::end(items) - std::begin(items);
    auto batches = createBatches(0, size, workers_.size(), nBatches);
    for (const auto& batch : batches)
        this->push(doBatch, batch);
}

//! waits for all jobs to finish and checks for interruptions,
//! but does not join the threads.
inline void ThreadPool::wait()
{
    while (true) {
        if ( waitForWakeUpEvent() ) {
            if ( !this->allJobsDone() ) {
                this->clear();  // cancel all remaining jobs
                continue;       // wait for currently running jobs
            }
            if ( this->hasErrored() | this->allJobsDone() | isInterrupted() )
                break;
        }
        Rcout << "";
        std::this_thread::yield();
    }

    Rcout << "";
    this->rethrowExceptions();
}

//! waits for all jobs to finish and joins all threads.
inline void ThreadPool::join()
{
    this->wait();
    this->announceStop();
    this->joinWorkers();
}

//! clears the pool from all open jobs.
inline void ThreadPool::clear()
{
    jobs_.clear();
}

//! spawns a worker thread waiting for jobs to arrive.
inline void ThreadPool::startWorker()
{
    workers_.emplace_back([this] {
        std::function<void()> job;
        // observe thread pool; only stop after all jobs are done
        while (!stopped_ | !jobs_.empty()) {
            // must hold a lock while modifying shared variables
            std::unique_lock<std::mutex> lk(mTasks_);

            // thread should wait when there is no job
            cvTasks_.wait(lk, [this] {
                return stopped_ || !jobs_.empty();
            });

            // queue can be empty if thread pool is stopped
            if (jobs_.empty())
                continue;

            // take job from the queue
            auto job = jobs_.pop();

            // lock can be released before starting work, but must signal
            // that thread will be busy before (!) to avoid premature breaks
            this->announceBusy();
            lk.unlock();

            this->doJob(std::move(job));
            this->announceIdle();
            std::this_thread::yield();
        }
    });
}

//! executes a job safely and let's pool know when it's busy.
//! @param job job to be exectued.
inline void ThreadPool::doJob(std::function<void()>&& job)
{
    // checkUserInterrupt();
    try {
        job();
    } catch (...) {
        std::lock_guard<std::mutex> lk(mTasks_);
        error_ptr_ = std::current_exception();
    }
}

//! signals that a worker is busy (must be called why locking mTasks).
inline void ThreadPool::announceBusy()
{
    ++numBusy_;
    cvBusy_.notify_one();
}

//! signals that a worker is idle.
inline void ThreadPool::announceIdle()
{
    {
        std::lock_guard<std::mutex> lk(mTasks_);
        --numBusy_;
    }
    cvBusy_.notify_one();
}

//! signals threads that no more new work is coming.
inline void ThreadPool::announceStop()
{
    {
        std::unique_lock<std::mutex> lk(mTasks_);
        stopped_ = true;
    }
    cvTasks_.notify_all();
}

//! joins worker threads if possible.
inline void ThreadPool::joinWorkers()
{
    if (workers_.size() > 0) {
        for (auto &worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
    }
}

//! checks if an error occured.
inline bool ThreadPool::hasErrored()
{
    return static_cast<bool>(error_ptr_);
}

//! check whether all jobs are done
inline bool ThreadPool::allJobsDone()
{
    return (numBusy_ == 0) && jobs_.empty();
}

//! checks whether wait() needs to wake up
inline bool ThreadPool::waitForWakeUpEvent()
{
    static auto timeout = std::chrono::milliseconds(50);
    auto isWakeUpEvent = [this] {
        return this->allJobsDone() | this->hasErrored();
    };
    std::unique_lock<std::mutex> lk(mTasks_);
    cvBusy_.wait_for(lk, timeout, isWakeUpEvent);
    return isWakeUpEvent();
}

//! rethrows exceptions (exceptions from workers are caught and stored; the
//! wait loop only checks, but does not throw for interruptions)
inline void ThreadPool::rethrowExceptions()
{
    checkUserInterrupt();
    if ( this->hasErrored() )
        std::rethrow_exception(error_ptr_);
}

}
