// Copyright © 2021 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include "RcppThread/Batch.hpp"
#include "RcppThread/RMonitor.hpp"
#include "RcppThread/Rcout.hpp"
#include "moodyCamel/blockingconcurrentqueue.h"

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
    void waitForJobs(moodycamel::ConsumerToken& tk);
    void processJobs(moodycamel::ConsumerToken& tk);
    void announceStop();
    void joinWorkers();

    bool hasErrored();
    bool allJobsDone();
    void waitForEvents();
    void rethrowExceptions();

    std::vector<std::thread> workers_;
    moodycamel::BlockingConcurrentQueue<std::function<void()>> jobs_{ 1024 };

    // variables for synchronization between workers
    std::mutex mDone_;
    std::condition_variable cvDone_;
    std::atomic_size_t numJobs_{ 0 };
    std::atomic_bool stopped_{ false };
    std::exception_ptr errorPtr_;
};

//! constructs a thread pool with as many workers as there are cores.
inline ThreadPool::ThreadPool()
  : ThreadPool(std::thread::hardware_concurrency())
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
    } catch (...) {
    }
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
    using Task = std::function<void()>;
    if (workers_.size() == 0) {
        f(args...); // if there are no workers, do the job in the main thread
    } else {
        if (stopped_)
            throw std::runtime_error("cannot push to joined thread pool");
        numJobs_.fetch_add(1, std::memory_order_release);
        jobs_.enqueue([f, args...] { f(args...); });
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
    using jobPackage = std::packaged_task<decltype(f(args...))()>;
    auto job =
      std::make_shared<jobPackage>([&f, args...] { return f(args...); });
    this->push([job] { (*job)(); });

    return job->get_future();
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
inline void
ThreadPool::parallelFor(ptrdiff_t begin, ptrdiff_t end, F&& f, size_t nBatches)
{
    if (end < begin)
        throw std::range_error(
          "end is less than begin; cannot run backward loops.");
    auto doBatch = [f](const Batch& b) {
        for (ptrdiff_t i = b.begin; i < b.end; i++)
            f(i);
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
inline void
ThreadPool::parallelForEach(I& items, F&& f, size_t nBatches)
{
    auto doBatch = [f, &items](const Batch& b) {
        for (ptrdiff_t i = b.begin; i < b.end; i++)
            f(items[i]);
    };
    size_t size = std::end(items) - std::begin(items);
    auto batches = createBatches(0, size, workers_.size(), nBatches);
    for (const auto& batch : batches)
        this->push(doBatch, batch);
}

//! waits for all jobs to finish and checks for interruptions,
//! but does not join the threads.
inline void
ThreadPool::wait()
{
    while (numJobs_.load(std::memory_order_acquire) != 0) {
        waitForEvents(); // non-spinning wait for mDone_
        if (this->hasErrored()) {
            this->announceStop(); // stop thread pool
            continue;             // wait for currently running jobs
        }
        if (this->hasErrored() | isInterrupted())
            break;
        Rcout << "";
        std::this_thread::yield();
    }
    // Rcout << "rethrowing exceptions" << std::endl;

    Rcout << "";
    // this->rethrowExceptions();
}

//! waits for all jobs to finish and joins all threads.
inline void
ThreadPool::join()
{
    this->wait();
    this->announceStop();
    this->joinWorkers();
}

//! clears the pool from all open jobs.
inline void
ThreadPool::clear()
{
    std::function<void()> job;
    while (numJobs_.load(std::memory_order_acquire) != 0) {
        numJobs_.fetch_sub(jobs_.try_dequeue(job), std::memory_order_release);
    }
}

//! spawns a worker thread waiting for jobs to arrive.
inline void
ThreadPool::startWorker()
{
    workers_.emplace_back([this] {
        thread_local moodycamel::ConsumerToken tk(jobs_);
        std::function<void()> job;
        while (!stopped_.load(std::memory_order_relaxed)) {
            this->waitForJobs(tk);
            this->processJobs(tk);
            // if all jobs are done, notify potentially waiting threads
            if (!numJobs_.load(std::memory_order_acquire))
                cvDone_.notify_all();
        }
    });
}

//! blocking wait for elements in the queue; also processes first job.
inline void
ThreadPool::waitForJobs(moodycamel::ConsumerToken& tk)
{
    std::function<void()> job;
    jobs_.wait_dequeue(tk, job);
    // popped job needs to be done here
    if (!stopped_.load(std::memory_order_relaxed))
        this->doJob(std::move(job));
}

//! process jobs until none are left.
inline void
ThreadPool::processJobs(moodycamel::ConsumerToken& tk)
{
    std::function<void()> job;
    while ((numJobs_.load(std::memory_order_acquire) != 0) &&
           !stopped_.load(std::memory_order_relaxed)) {
        std::function<void()> job;
        if (jobs_.try_dequeue(tk, job))
            this->doJob(std::move(job));
    }
}

//! executes a job safely and decrements the job count.
//! @param job job to be exectued.
inline void
ThreadPool::doJob(std::function<void()>&& job)
{
    try {
        job();
        numJobs_.fetch_sub(1, std::memory_order_release);
    } catch (...) {
        {
            std::lock_guard<std::mutex> lk(mDone_);
            errorPtr_ = std::current_exception();
        }
        cvDone_.notify_all();
    }
}

//! signals threads that no more new work is coming.
inline void
ThreadPool::announceStop()
{
    stopped_ = true;
    // push empty jobs to wake up waiting workers
    for (size_t i = 0; i < workers_.size(); i++) {
        numJobs_.fetch_add(1, std::memory_order_release);
        jobs_.enqueue([] {});
    }
}

//! joins worker threads if possible.
inline void
ThreadPool::joinWorkers()
{
    if (workers_.size() > 0) {
        for (auto& worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
    }
}

//! checks if an error occured.
inline bool
ThreadPool::hasErrored()
{
    return static_cast<bool>(errorPtr_);
}

//! check whether all jobs are done
inline bool
ThreadPool::allJobsDone()
{
    // acquire might prevent an unncessary loop
    return numJobs_.load(std::memory_order_acquire) == 0;
}

//! checks whether wait() needs to wake up
inline void
ThreadPool::waitForEvents()
{
    static auto timeout = std::chrono::milliseconds(50);
    auto isWakeUpEvent = [this] {
        return this->allJobsDone() | this->hasErrored();
    };
    std::unique_lock<std::mutex> lk(mDone_);
    cvDone_.wait_for(lk, timeout, isWakeUpEvent);
}

//! rethrows exceptions (exceptions from workers are caught and stored; the
//! wait loop only checks, but does not throw for interruptions)
inline void
ThreadPool::rethrowExceptions()
{
    checkUserInterrupt();
    std::rethrow_exception(errorPtr_);
}
}
