// Copyright © 2018 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include "RcppThread/RMonitor.hpp"
#include "RcppThread/Rcout.hpp"

#include <vector>
#include <thread>
#include <queue>
#include <condition_variable>
#include <future>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <cmath>

namespace RcppThread {

struct Batch {
    ptrdiff_t begin;
    ptrdiff_t end;
};


inline size_t computeBatchSize(size_t nTasks, size_t nThreads)
{
    if (nTasks < nThreads)
        return nTasks;
    return nThreads * (1 + std::floor(std::log(nTasks / nThreads)));
}


inline std::vector<Batch> createBatches(ptrdiff_t begin,
                                        size_t nTasks,
                                        size_t nThreads,
                                        size_t nBatches)
{
    nThreads = std::max(nThreads, static_cast<size_t>(1));
    if (nBatches == 0)
        nBatches = computeBatchSize(nTasks, nThreads);
    nBatches = std::min(nTasks, nBatches);
    std::vector<Batch> batches(nBatches);
    size_t    minSize = nTasks / nBatches;
    ptrdiff_t remSize = nTasks % nBatches;

    for (size_t i = 0, k = 0; i < nTasks; k++) {
        ptrdiff_t bBegin = begin + i;
        ptrdiff_t bSize  = minSize + (remSize-- > 0);
        batches[k] = Batch{bBegin, bBegin + bSize};
        i += bSize;
    }

    return batches;
}


//! Implemenation of the thread pool pattern based on `Thread`.
class ThreadPool {
public:

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool(const ThreadPool&) = delete;

    //! constructs a thread pool with as many workers as there are cores.
    ThreadPool() : ThreadPool(std::thread::hardware_concurrency())
    {}

    //! constructs a thread pool with `nThreads` threads.
    //! @param nThreads number of threads to create; if `nThreads = 0`, all
    //!    work pushed to the pool will be done in the main thread.
    ThreadPool(size_t nThreads)
    {
        for (size_t t = 0; t < nThreads; ++t) {
            workers_.emplace_back([this] {
                std::function<void()> job;
                // observe thread pool; only stop after all jobs are done
                while (!stopped_ | !jobs_.empty()) {
                    {
                        // must hold a lock while modifying shared variables
                        std::unique_lock<std::mutex> lk(m_);

                        // thread should wait when there is no job
                        cvTasks_.wait(lk, [this] {
                            return stopped_ || !jobs_.empty();
                        });

                        // queue can be empty if thread pool is stopped
                        if (jobs_.empty())
                            continue;

                        // take job from the queue
                        job = std::move(jobs_.front());
                        jobs_.pop();
                    }

                    this->doJob(std::move(job));
                }
            });
        }
    }

    ~ThreadPool() noexcept
    {
        // destructors should never throw
        try {
            this->joinWorkers();
        } catch (...) {}
    }

    // assignment operators
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = default;

    //! pushes new jobs to the thread pool.
    //! @param f a function taking an arbitrary number of arguments.
    //! @param args a comma-seperated list of the other arguments that shall
    //!   be passed to `f`.
    //! @return an `std::shared_future`, where the user can get the result and
    //!   rethrow the catched exceptions.
    template<class F, class... Args>
    auto push(F&& f, Args&&... args) -> std::shared_future<decltype(f(args...))>
    {
        using result_t = decltype(f(args...));
        using jobPackage = std::packaged_task<result_t()>;

        // create packaged task on the heap
        auto job = std::make_shared<jobPackage>([&f, args...] {
            return f(args...);
        });
        std::shared_future<result_t> sf(std::move(job->get_future()));

        // if there are no workers, just do the job in the main thread
        if (workers_.size() == 0) {
            (*job)();
            sf.get();
            return sf;
        }

        // add job to the queue

        {
            std::unique_lock<std::mutex> lk(m_);
            if (stopped_)
                throw std::runtime_error("cannot push to joined thread pool");
            jobs_.emplace([job] () { (*job)(); });
        }

        // signal a waiting worker that there's a new job
        cvTasks_.notify_one();

        // return future result of the job
        return sf;
    }

    //! maps a function on a list of items, possibly running tasks in parallel.
    //! @param f function to be mapped.
    //! @param items an objects containing the items on which `f` shall be
    //!   mapped; must allow for `auto` loops (i.e., `std::begin(I)`/
    //!  `std::end(I)` must be defined).
    template<class F, class I>
    void map(F&& f, I &&items)
    {
        for (auto &&item : items)
            this->push(f, item);
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
    inline void parallelFor(ptrdiff_t begin, size_t size, F&& f,
                            size_t nBatches = 0)
    {
        static auto func = std::move(f);
        auto doBatch = [&] (const Batch& b) {
            for (ptrdiff_t i = b.begin; i < b.end; i++)
                func(i);
        };
        auto batches = createBatches(begin, size, workers_.size(), nBatches);
        this->map(doBatch, std::move(batches));
    }

    //! computes a range-based for loop in parallel batches.
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
    //! pool.forEach(x, [&] (double& xx) {
    //!     xx *= 2;
    //! });
    //! ```
    //! **Caution**: if the iterations are not independent from another,
    //! the tasks need to be synchronized manually (e.g., using mutexes).
    template<class F, class I>
    inline void forEach(I&& items, F&& f, size_t nBatches = 0)
    {
        size_t size = std::end(items) - std::begin(items);
        this->parallelFor(0, size, f, nBatches);
    }

    //! waits for all jobs to finish and joins all threads.
    void join()
    {
        this->announceStop();
        this->wait();
        this->joinWorkers();
    }

    //! waits for all jobs to finish and checks for interruptions,
    //! but does not join the threads.
    void wait()
    {
        if (workers_.size() == 0) {
            checkUserInterrupt();
            return;
        }

        auto workLeft = [this] { return (numBusy_ == 0) && jobs_.empty(); };
        auto timeout = std::chrono::milliseconds(250);

        while (true) {
            {
                std::unique_lock<std::mutex> lk(m_);
                cvBusy_.wait_for(lk, timeout, workLeft);
            }
            Rcout << "";
            if ( isInterrupted() ) {
                this->clear();
                break;
            }
            if ( workLeft() ){
                break;
            }
        }
        Rcout << "";
        checkUserInterrupt();
    }

    //! clears the pool from all open jobs.
    void clear()
    {
        // remove all remaining jobs
        std::lock_guard<std::mutex> lk(m_);
        std::queue<std::function<void()>>().swap(jobs_);
        cvTasks_.notify_all();
    }

private:
    //! executes a job savely and lets pool no when it's working.
    void doJob(std::function<void()>&& job)
    {
        checkUserInterrupt();
        announceBusy();
        try {
            job();
        } catch (const std::exception& e) {
            announceIdle();
            throw e;
        }
        announceIdle();
    }

    //! signals that the worker is busy.
    void announceBusy()
    {
        {
            std::unique_lock<std::mutex> lk(m_);
            ++numBusy_;
        }
        cvBusy_.notify_one();
    }

    //! signals that the worker is idle.
    void announceIdle()
    {
        {
            std::unique_lock<std::mutex> lk(m_);
            --numBusy_;
        }
        cvBusy_.notify_one();
        std::this_thread::yield();
    }

    //! signals threads that no more new work is coming
    void announceStop()
    {
        {
            std::unique_lock<std::mutex> lk(m_);
            stopped_ = true;
        }
        cvTasks_.notify_all();
    }

    //! joins threads if not done already
    void joinWorkers()
    {
        if (workers_.size() > 0) {
            for (auto &worker : workers_) {
                if (worker.joinable())
                    worker.join();
            }
        }
    }

    std::vector<std::thread> workers_;        // worker threads in the pool
    std::queue<std::function<void()>> jobs_;  // the task queue

    // variables for synchronization between workers
    std::mutex m_;
    std::condition_variable cvTasks_;
    std::condition_variable cvBusy_;
    size_t numBusy_{0};
    bool stopped_{false};
};

}
