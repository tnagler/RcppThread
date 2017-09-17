#pragma once

#include <thread>
#include <future>
#include <functional>
#include "RcppThreads/RMonitor.hpp"

namespace RcppThreads {

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

    //! checks for interrupt + msgs every `checkEvery` milliseconds and after
    //! computations have finished.
    //! @param checkEvery time between periodic checks in milliseconds.
    void join(size_t checkEvery = 500)
    {
        auto timeout = std::chrono::milliseconds(checkEvery);
        while (future_.wait_for(timeout) != std::future_status::ready) {
            releaseMsgBuffer();
            checkInterrupt();
        }
        releaseMsgBuffer();
        checkInterrupt();
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

    static unsigned int hardware_concurrency()
    {
        return std::thread::hardware_concurrency();
    }

private:
    std::thread thread_;        //! underlying std::thread.
    std::future<void> future_;  //! future result of task passed to the thread.
};

}
