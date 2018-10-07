// Copyright © 2018 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include "RcppThread/RMonitor.hpp"
#include "RcppThread/Rcout.hpp"

#include <thread>
#include <future>
#include <functional>

//! `RcppThread` functionality
namespace RcppThread {

//! @brief R-friendly version of `std::thread`.
//!
//! Instances of class `Thread` behave just like instances of `std::thread`,
//! see http://en.cppreference.com/w/cpp/thread/thread for methods and examples.
//! There is one difference exception: Whenever other threads are doing some
//! work, the master thread  periodically synchronizes with R. When the user
//! interrupts a threaded computation, any thread will stop as soon as it
//! encounters a `checkUserInterrupt()`.
//!
class Thread {
public:
    Thread() = default;
    Thread(Thread&) = delete;
    Thread(const Thread&) = delete;
    Thread(Thread&& other)
    {
        swap(other);
    }

    ~Thread()
    {
        if (thread_.joinable())
            thread_.join();
    }

    template<class Function, class... Args> explicit
    Thread(Function&& f, Args&&... args)
    {
        auto f0 = [=] () {
            if ( !isInterrupted() )
                f(args...);
        };
        auto task = std::packaged_task<void()>(f0);
        future_ = task.get_future();
        thread_ = std::thread(std::move(task));
    }

    Thread& operator=(const Thread&) = delete;
    Thread& operator=(Thread&& other)
    {
        if (thread_.joinable())
            std::terminate();
        swap(other);
        return *this;
    }

    void swap(Thread& other) noexcept
    {
        std::swap(thread_, other.thread_);
        std::swap(future_, other.future_);
    }

    bool joinable() const
    {
        return thread_.joinable();
    }

    // checks for interrupt + msgs every 0.25 seconds and after
    // computations have finished.
    void join()
    {
        auto timeout = std::chrono::milliseconds(250);
        while (future_.wait_for(timeout) != std::future_status::ready) {
            Rcout << "";
            isInterrupted();
        }
        Rcout << "";
        checkUserInterrupt();
        thread_.join();
    }

    void detach()
    {
        thread_.detach();
    }

    std::thread::id get_id() const
    {
        return thread_.get_id();
    }

    typedef __gthread_t native_handle_type;
    native_handle_type native_handle()
    {
        return thread_.native_handle();
    }

    static unsigned int hardware_concurrency()
    {
        return std::thread::hardware_concurrency();
    }

private:
    std::thread thread_;        //! underlying std::thread.
    std::future<void> future_;  //! future result of task passed to the thread.
};

}

// override std::thread to use RcppThread::Thread instead
#ifndef RCPPTHREAD_OVERRIDE_THREAD
#define RCPPTHREAD_OVERRIDE_THREAD 1
#endif

#if RCPPTHREAD_OVERRIDE_THREAD
    #define thread RcppThreadThreadThread
    namespace std {
        using RcppThreadThreadThread = RcppThread::Thread;
    }
    namespace ttread {
        using RcppThreadThreadThread = RcppThread::Thread;
    }
#endif
