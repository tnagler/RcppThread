// Copyright 2021 Thomas Nagler (MIT License)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include <ctime>

std::string
get_time()
{
    using namespace std::chrono;
    auto time = system_clock::to_time_t(system_clock::now());
    return "| " + std::string(std::ctime(&time));
}

//! tpool namespace
namespace tpool {

//! @brief Finish line - a synchronization primitive.
//!
//! Lets some threads wait until others reach a control point. Start a runner
//! with `FinishLine::start()`, and wait for all runners to finish with
//! `FinishLine::wait()`.>
class FinishLine
{
  public:
    //! constructs a finish line.
    //! @param runners number of initial runners.
    FinishLine(size_t runners = 0) noexcept
      : runners_(runners)
    {}

    ~FinishLine() { std::cout << "~FinishLine() " << get_time() << std::endl; }

    //! adds runners.
    //! @param runners adds runners to the race.
    void add(size_t runners = 1) noexcept { runners_ = runners_ + runners; }

    //! adds a single runner.
    void start() noexcept { ++runners_; }

    //! indicates that a runner has crossed the finish line.
    void cross() noexcept
    {
        if (--runners_ <= 0) {
            std::lock_guard<std::mutex> lk(mtx_);
            cv_.notify_all();
        }
    }

    bool all_finished() const { return runners_ <= 0; }

    //! waits for all active runners to cross the finish line.
    void wait() noexcept
    {
        std::this_thread::yield();
        std::unique_lock<std::mutex> lk(mtx_);
        while ((runners_ > 0) && !exception_ptr_)
            cv_.wait(lk);

        if (exception_ptr_)
            std::rethrow_exception(exception_ptr_);
    }

    //! waits for all active runners to cross the finish line.
    //! @param duration maximal waiting time (as `std::chrono::duration`).
    template<typename Duration>
    void wait_for(const Duration& duration) noexcept
    {
        std::unique_lock<std::mutex> lk(mtx_);
        while ((runners_ > 0) && !exception_ptr_)
            cv_.wait_for(lk, duration);
        if (exception_ptr_)
            std::rethrow_exception(exception_ptr_);
    }

    //! aborts the race.
    //! @param eptr (optional) pointer to an active exception to be rethrown by
    //! a waiting thread; typically retrieved from `std::current_exception()`.
    void abort(std::exception_ptr eptr = nullptr) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);
        runners_ = 0;
        exception_ptr_ = eptr;
        cv_.notify_all();
    }

  private:
    alignas(64) std::atomic<size_t> runners_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::exception_ptr exception_ptr_{ nullptr };
};

//! Implementation details
namespace detail {

//! A simple ring buffer class.
template<typename T>
class RingBuffer
{
  public:
    explicit RingBuffer(size_t capacity)
      : buffer_{ std::unique_ptr<T[]>(new T[capacity]) }
      , capacity_{ capacity }
      , mask_{ capacity - 1 }

    {
        if (capacity_ & (capacity_ - 1))
            throw std::runtime_error("capacity must be a power of two");
    }

    size_t capacity() const { return capacity_; }

    void set_entry(size_t i, T val) { buffer_[i & mask_] = val; }

    T get_entry(size_t i) const { return buffer_[i & mask_]; }

    RingBuffer<T>* enlarge(size_t bottom, size_t top) const
    {
        RingBuffer<T>* new_buffer = new RingBuffer{ 2 * capacity_ };
        for (size_t i = top; i != bottom; ++i)
            new_buffer->set_entry(i, this->get_entry(i));
        return new_buffer;
    }

  private:
    std::unique_ptr<T[]> buffer_;
    size_t capacity_;
    size_t mask_;
};

// exchange is not available in C++11, use implementatino from
// https://en.cppreference.com/w/cpp/utility/exchange
template<class T>
T
exchange(T& obj, T&& new_value) noexcept
{
    T old_value = std::move(obj);
    obj = std::forward<T>(new_value);
    return old_value;
}

//! A multi-producer, multi-consumer queue; pops are lock free.
class TaskQueue
{
    using Task = std::function<void()>;

  public:
    //! constructs the que with a given capacity.
    //! @param capacity must be a power of two.
    TaskQueue(size_t capacity = 256)
      : buffer_{ new RingBuffer<Task*>(capacity) }
    {}

    ~TaskQueue() noexcept
    {
        std::cout << "~TaskQueue() " << get_time() << std::endl;
        // must free memory allocated by push(), but not deallocated by pop()
        auto buf_ptr = buffer_.load();
        for (int i = top_; i < bottom_.load(m_relaxed); ++i)
            delete buf_ptr->get_entry(i);
        delete buf_ptr;
    }

    TaskQueue(TaskQueue const& other) = delete;
    TaskQueue& operator=(TaskQueue const& other) = delete;

    //! queries the size.
    size_t size() const
    {
        auto b = bottom_.load(m_relaxed);
        auto t = top_.load(m_relaxed);
        return static_cast<size_t>(b >= t ? b - t : 0);
    }

    //! checks if queue is empty.
    bool empty() const { return (this->size() == 0); }

    //! clears the queue.
    void clear()
    {
        auto buf_ptr = buffer_.load();
        auto b = bottom_.load(m_relaxed);
        int t;
        while (true) {
            t = top_.load(m_relaxed);
            if (top_.compare_exchange_weak(t, b, m_release, m_relaxed))
                break;
        }
        for (int i = t; i < b; ++i)
            delete buf_ptr->get_entry(i);
    }

    //! pushes a task to the bottom of the queue; returns false if queue is
    //! currently locked; enlarges the queue if full.
    bool try_push(Task&& task)
    {
        // must hold lock in case there are multiple producers, abort if already
        // taken, so we can check out next queue
        std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
        if (!lk)
            return false;

        auto b = bottom_.load(m_relaxed);
        auto t = top_.load(m_acquire);
        RingBuffer<Task*>* buf_ptr = buffer_.load(m_relaxed);

        if (static_cast<int>(buf_ptr->capacity()) < (b - t) + 1) {
            old_buffers_.emplace_back(
              exchange(buf_ptr, buf_ptr->enlarge(b, t)));
            buffer_.store(buf_ptr, m_relaxed);
        }

        Task* task_ptr = new Task{ std::forward<Task>(task) };
        buf_ptr->set_entry(b, task_ptr);

        std::atomic_thread_fence(m_release);
        bottom_.store(b + 1, m_relaxed);

        return true;
    }

    //! pops a task from the top of the queue; returns false if lost race.
    bool try_pop(Task& task)
    {
        auto t = top_.load(m_acquire);
        std::atomic_thread_fence(m_seq_cst);
        auto b = bottom_.load(m_acquire);

        if (t < b) {
            // must load task pointer before acquiring the slot
            auto task_ptr = buffer_.load(m_acquire)->get_entry(t);
            if (top_.compare_exchange_strong(t, t + 1, m_seq_cst, m_relaxed)) {
                task = std::move(*task_ptr);
                delete task_ptr;
                return true; // won race
            }
        }
        return false; // queue is empty or lost race
    }

  private:
    alignas(64) std::atomic_int top_{ 0 };
    alignas(64) std::atomic_int bottom_{ 0 };
    alignas(64) std::atomic<RingBuffer<Task*>*> buffer_{ nullptr };
    std::vector<std::unique_ptr<RingBuffer<Task*>>> old_buffers_;
    std::mutex mutex_;

    // convenience aliases
    static constexpr std::memory_order m_relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order m_acquire = std::memory_order_acquire;
    static constexpr std::memory_order m_release = std::memory_order_release;
    static constexpr std::memory_order m_seq_cst = std::memory_order_seq_cst;
    static constexpr std::memory_order m_consume = std::memory_order_consume;
};

//! Task manager based on work stealing
struct TaskManager
{
    std::mutex m_;
    std::vector<TaskQueue> queues_;
    std::condition_variable cv_;
    std::atomic_bool stopped_{ false };
    alignas(64) std::atomic_size_t push_idx_{ 0 };
    size_t num_queues_;

    TaskManager(size_t num_queues)
      : queues_{ std::vector<TaskQueue>(num_queues) }
      , num_queues_{ num_queues }
    {}

    ~TaskManager()
    {
        this->stop();
        std::cout << "~TaskManager()" << std::endl;
    }

    template<typename Task>
    void push(Task&& task)
    {
        while (!stopped_ && !queues_[push_idx_++ % num_queues_].try_push(task))
            continue;
        cv_.notify_all();
    }

    bool empty()
    {
        for (auto& q : queues_) {
            if (!q.empty())
                return false;
        }
        return true;
    }

    template<typename Task>
    bool try_pop(Task& task, size_t worker_id = 0)
    {
        do {
            if (!stopped_ && queues_[worker_id++ % num_queues_].try_pop(task))
                return true;
        } while (!this->empty() && !stopped_);
        return false;
    }

    void clear()
    {
        for (auto& q : queues_)
            q.clear();
    }

    bool stopped() { return stopped_; }

    void wait_for_jobs()
    {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this] { return !this->empty() || stopped_; });
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            stopped_ = true;
            this->clear();
        }
        cv_.notify_all();
    }
};

} // end namespace detail

}
