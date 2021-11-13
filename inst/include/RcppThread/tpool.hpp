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

//! tpool namespace
namespace tpool {

//! @brief Finish line - a synchronization primitive.
//!
//! Lets some threads wait until others reach a control point. Start a runner
//! with `FinishLine::start()`, and wait for all runners to finish with
//! `FinishLine::wait()`.
class FinishLine
{
  public:
    //! constructs a finish line.
    //! @param runners number of initial runners.
    FinishLine(size_t runners = 0) noexcept
      : runners_(runners)
    {}

    //! adds runners.
    //! @param runners adds runners to the race.
    void add(size_t runners = 1) noexcept { runners_ = runners_ + runners; }

    //! adds a single runner.
    void start() noexcept { ++runners_; }

    //! indicates that a runner has crossed the finish line.
    void cross() noexcept
    {
        if (--runners_ <= 0) {
            cv_.notify_all();
        }
    }

    //! waits for all active runners to cross the finish line.
    void wait() noexcept
    {
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
      : capacity_{ capacity }
      , mask_{ capacity - 1 }
      , buffer_{ std::unique_ptr<T[]>(new T[capacity]) }
    {
        if (capacity_ & (capacity_ - 1))
            throw std::runtime_error("capacity must be a power of two");
    }

    size_t capacity() const { return capacity_; }

    void store(size_t i, T&& x) { buffer_[i & mask_] = std::move(x); }

    T load(size_t i) const { return buffer_[i & mask_]; }

    // creates a new ring buffer with pointers to current elements.
    RingBuffer<T>* enlarge(size_t bottom, size_t top) const
    {
        RingBuffer<T>* new_buffer = new RingBuffer{ 2 * capacity_ };
        for (size_t i = top; i != bottom; ++i)
            new_buffer->store(i, this->load(i));
        return new_buffer;
    }

  private:
    std::unique_ptr<T[]> buffer_;
    size_t capacity_;
    size_t mask_;
};

// exchange is not available in C++11, use implementatino from
// https://en.cppreference.com/w/cpp/utility/exchange
template<class T, class U = T>
T
exchange(T& obj, U&& new_value) noexcept
{
    T old_value = std::move(obj);
    obj = std::forward<U>(new_value);
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
      : buffer_{ new RingBuffer<Task>(capacity) }
    {}

    ~TaskQueue() noexcept { delete buffer_.load(); }
    TaskQueue(TaskQueue const& other) = delete;
    TaskQueue& operator=(TaskQueue const& other) = delete;

    //! queries the size.
    size_t size() const
    {
        auto b = bottom_.load(m_relaxed);
        auto t = top_.load(m_relaxed);
        return static_cast<size_t>(b >= t ? b - t : 0);
    }

    //! queries the capacity.
    size_t capacity() const { return buffer_.load(m_relaxed)->capacity(); }

    //! checks if queue is empty.
    bool empty() const { return (this->size() == 0); }

    //! clears the queue.
    void clear()
    {
        auto b = bottom_.load(m_relaxed);
        top_.store(b, m_release);
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
        RingBuffer<Task>* buf_ptr = buffer_.load(m_relaxed);

        if (buf_ptr->capacity() < (b - t) + 1) {
            old_buffers_.emplace_back(
              exchange(buf_ptr, buf_ptr->enlarge(b, t)));
            buffer_.store(buf_ptr, m_relaxed);
        }

        buf_ptr->store(b, std::move(task));

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
            task = buffer_.load(m_consume)->load(t);
            if (top_.compare_exchange_strong(t, t + 1, m_seq_cst, m_relaxed)) {
                return true; // won race
            }
        }
        return false; // queue is empty or lost race
    }

  private:
    alignas(64) std::atomic_ptrdiff_t top_{ 0 };
    alignas(64) std::atomic_ptrdiff_t bottom_{ 0 };
    alignas(64) std::atomic<RingBuffer<Task>*> buffer_{ nullptr };
    std::vector<std::unique_ptr<RingBuffer<Task>>> old_buffers_;
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
    std::vector<TaskQueue> queues_;
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic_bool stopped_{ false };
    alignas(64) std::atomic_size_t push_idx_;
    alignas(64) std::atomic_size_t pop_idx_;
    size_t num_queues_;

    TaskManager(size_t num_queues = 1)
      : num_queues_{ num_queues }
      , queues_{ std::vector<TaskQueue>(num_queues) }
    {}

    template<typename Task>
    void push(Task&& task)
    {
        while (!queues_[push_idx_++ % num_queues_].try_push(task))
            continue;
        cv_.notify_one();
    }

    bool empty()
    {
        for (auto& q : queues_) {
            if (!q.empty())
                return false;
        }
        return true;
    }

    bool try_pop(std::function<void()>& task)
    {
        do {
            if (queues_[pop_idx_++ % num_queues_].try_pop(task))
                return true;
        } while (!this->empty());
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
        }
        cv_.notify_all();
        this->clear();
    }
};

} // end namespace detail

//! A work stealing thread pool.
class ThreadPool
{
  public:
    //! constructs a thread pool.
    //! @param n_workers number of worker threads to create; defaults to number
    //! of available (virtual) hardware cores.
    explicit ThreadPool(
      size_t num_threads = std::thread::hardware_concurrency());

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;
    ~ThreadPool() noexcept;

    //! @brief returns a reference to the global thread pool instance.
    static ThreadPool& global_instance()
    {
        static ThreadPool instance_;
        return instance_;
    }

    //! @brief pushes a job to the thread pool.
    //! @param f a function.
    //! @param args (optional) arguments passed to `f`.
    template<class Function, class... Args>
    void push(Function&& f, Args&&... args);

    //! @brief executes a job asynchronously the global thread pool.
    //! @param f a function.
    //! @param args (optional) arguments passed to `f`.
    //! @return A `std::future` for the task. Call `future.get()` to retrieve
    //! the results at a later point in time (blocking).
    template<class Function, class... Args>
    auto async(Function&& f, Args&&... args)
      -> std::future<decltype(f(args...))>;

    //! @brief waits for all jobs currently running on the global thread pool.
    void wait();
    //! @brief clears all jobs currently running on the global thread pool.
    void clear();

  private:
    void start_worker();
    void join_workers();
    template<class Task>
    void execute_safely(Task& task);

    std::vector<std::thread> workers_;
    detail::TaskManager task_manager_;
    size_t n_workers_;
    std::exception_ptr error_ptr_{ nullptr };
    FinishLine finish_line_{ 0 };
};

inline ThreadPool::ThreadPool(size_t n_workers)
  : n_workers_{ n_workers }
{
    for (size_t id = 0; id < n_workers; ++id)
        this->start_worker();
}

inline ThreadPool::~ThreadPool() noexcept
{
    try {
        task_manager_.stop();
        this->join_workers();
    } catch (...) {
        // destructors should never throw
    }
}

template<class Function, class... Args>
void
ThreadPool::push(Function&& f, Args&&... args)
{
    task_manager_.push(
      std::bind(std::forward<Function>(f), std::forward<Args>(args)...));
}

template<class Function, class... Args>
auto
ThreadPool::async(Function&& f, Args&&... args)
  -> std::future<decltype(f(args...))>
{
    using task = std::packaged_task<decltype(f(args...))()>;
    auto pack =
      std::bind(std::forward<Function>(f), std::forward<Args>(args)...);
    auto task_ptr = std::make_shared<task>(std::move(pack));
    task_manager_.push([task_ptr] { (*task_ptr)(); });
    return task_ptr->get_future();
}

void
ThreadPool::wait()
{
    while (!task_manager_.empty())
        finish_line_.wait();
}

void
ThreadPool::clear()
{
    task_manager_.clear();
}

inline void
ThreadPool::start_worker()
{
    workers_.emplace_back([this] {
        std::function<void()> task;
        while (!task_manager_.stopped()) {
            task_manager_.wait_for_jobs();

            finish_line_.start();
            while (task_manager_.try_pop(task))
                execute_safely(task);
            finish_line_.cross();
        }
    });
}

template<class Task>
inline void
ThreadPool::execute_safely(Task& task)
{
    try {
        task();
    } catch (...) {
        finish_line_.abort(std::current_exception());
    }
}

inline void
ThreadPool::join_workers()
{
    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
}

//! Direct access to the global thread pool ------------------------------------

//! @brief push a job to the global thread pool.
//! @param f a function.
//! @param args (optional) arguments passed to `f`.
template<class Function, class... Args>
void
push(Function&& f, Args&&... args)
{
    ThreadPool::global_instance().push(std::forward<Function>(f),
                                       std::forward<Args>(args)...);
}

//! @brief executes a job asynchronously the global thread pool.
//! @param f a function.
//! @param args (optional) arguments passed to `f`.
//! @return A `std::future` for the task. Call `future.get()` to retrieve the
//! results at a later point in time (blocking).
template<class Function, class... Args>
auto
async(Function&& f, Args&&... args) -> std::future<decltype(f(args...))>
{
    return ThreadPool::global_instance().async(std::forward<Function>(f),
                                               std::forward<Args>(args)...);
}

//! @brief waits for all jobs currently running on the global thread pool.
void
wait()
{
    ThreadPool::global_instance().wait();
}

//! @brief clears all jobs currently running on the global thread pool.
void
clear()
{
    ThreadPool::global_instance().clear();
}

}