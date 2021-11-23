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

//! quickpool namespace
namespace quickpool {

namespace detail {
template<typename T>
class aligned_atomic : public std::atomic<T>
{
  public:
    explicit aligned_atomic(T value)
      : std::atomic<T>(value)
    {}

  private:
    using TT = std::atomic<T>;
    char padding_[64 > sizeof(TT) ? 64 - sizeof(TT) : 1];
};
}

//! @brief Todo list - a synchronization primitive.
//! @details Add a task with `add()`, cross it off with `cross()`, and wait for
//! all tasks to complete with `wait()`.
class TodoList
{
  public:
    //! constructs the todo list.
    //! @param num_tasks initial number of tasks.
    TodoList(size_t num_tasks = 0) noexcept
      : num_tasks_(num_tasks)
    {}

    //! adds tasks to the list.
    //! @param num_tasks add that many tasks to the list.
    void add(size_t num_tasks = 1) noexcept { num_tasks_.fetch_add(num_tasks); }

    //! crosses tasks from the list.
    //! @param num_tasks cross that many tasks to the list.
    void cross(size_t num_tasks = 1)
    {
        num_tasks_.fetch_sub(num_tasks);
        if (num_tasks_ <= 0) {
            {
                std::lock_guard<std::mutex> lk(mtx_); // must lock before signal
            }
            cv_.notify_all();
        }
    }

    //! checks whether list is empty.
    bool empty() const noexcept { return num_tasks_ <= 0; }

    //! waits for the list to be empty.
    //! @param millis if > 0; waiting aborts after waiting that many
    //! milliseconds.
    void wait(size_t millis = 0)
    {
        std::this_thread::yield();
        auto wake_up = [this] { return (num_tasks_ <= 0) || exception_ptr_; };
        std::unique_lock<std::mutex> lk(mtx_);
        if (millis == 0) {
            cv_.wait(lk, wake_up);
        } else {
            cv_.wait_for(lk, std::chrono::milliseconds(millis), wake_up);
        }
        if (exception_ptr_)
            std::rethrow_exception(exception_ptr_);
    }

    //! stops the list; it will forever look empty from now on.
    //! @param eptr (optional) pointer to an active exception to be rethrown by
    //! a waiting thread; typically retrieved from `std::current_exception()`.
    void stop(std::exception_ptr eptr = nullptr) noexcept
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            // Some threads may add() or cross() after we stop. The large
            // negative number prevents num_tasks_ from becoming positive again.
            num_tasks_.store(std::numeric_limits<int>::min() / 2);
            exception_ptr_ = eptr;
        }
        cv_.notify_all();
    }

  private:
    detail::aligned_atomic<int> num_tasks_{ 0 };
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

    {}

    size_t capacity() const { return capacity_; }

    void set_entry(size_t i, T val) { buffer_[i & mask_] = val; }

    T get_entry(size_t i) const { return buffer_[i & mask_]; }

    RingBuffer<T>* enlarged_copy(size_t bottom, size_t top) const
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
    //! constructs the queue with a given capacity.
    //! @param capacity must be a power of two.
    TaskQueue(size_t capacity = 256)
      : buffer_{ new RingBuffer<Task*>(capacity) }
    {}

    ~TaskQueue() noexcept
    {
        // must free memory allocated by push(), but not deallocated by pop()
        auto buf_ptr = buffer_.load();
        for (int i = top_; i < bottom_.load(m_relaxed); ++i)
            delete buf_ptr->get_entry(i);
        delete buf_ptr;
    }

    TaskQueue(TaskQueue const& other) = delete;
    TaskQueue& operator=(TaskQueue const& other) = delete;

    //! checks if queue is empty.
    bool empty() const
    {
        return (bottom_.load(m_relaxed) <= top_.load(m_relaxed));
    }

    //! pushes a task to the bottom of the queue; returns false if queue is
    //! currently locked; enlarges the queue if full.
    bool try_push(Task&& task)
    {
        {
            // must hold lock in case of multiple producers, abort if already
            // taken, so we can check out next queue
            std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
            if (!lk)
                return false;
            this->push_unsafe(std::forward<Task>(task));
        }
        cv_.notify_one();
        return true;
    }

    //! pushes a task to the bottom of the queue; enlarges the queue if full.
    void force_push(Task&& task)
    {
        {
            // must hold lock in case of multiple producers
            std::lock_guard<std::mutex> lk(mutex_);
            this->push_unsafe(std::forward<Task>(task));
        }
        cv_.notify_one();
    }

    //! pushes a task without locking the queue (enough for single producer)
    void push_unsafe(Task&& task)
    {
        auto b = bottom_.load(m_relaxed);
        auto t = top_.load(m_acquire);
        RingBuffer<Task*>* buf_ptr = buffer_.load(m_relaxed);

        if (static_cast<int>(buf_ptr->capacity()) < (b - t) + 1) {
            // buffer is full, create enlarged copy before continuing
            old_buffers_.emplace_back(
              exchange(buf_ptr, buf_ptr->enlarged_copy(b, t)));
            buffer_.store(buf_ptr, m_relaxed);
        }

        buf_ptr->set_entry(b, new Task{ std::forward<Task>(task) });
        bottom_.store(b + 1, m_release);
    }

    //! pops a task from the top of the queue; returns false if lost race.
    bool try_pop(Task& task)
    {
        auto t = top_.load(m_acquire);
        std::atomic_thread_fence(m_seq_cst);
        auto b = bottom_.load(m_acquire);

        if (t < b) {
            // must load task pointer before acquiring the slot, because it
            // could be overwritten immediately after
            auto task_ptr = buffer_.load(m_acquire)->get_entry(t);

            if (top_.compare_exchange_strong(t, t + 1, m_seq_cst, m_relaxed)) {
                task = std::move(*task_ptr); // won race, get task
                delete task_ptr; // fre memory allocated in push_unsafe()
                return true;
            }
        }
        return false; // queue is empty or lost race
    }

    void wait()
    {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return !this->empty() || stopped_; });
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

  private:
    detail::aligned_atomic<int> top_{ 0 };
    detail::aligned_atomic<int> bottom_{ 0 };
    std::atomic<RingBuffer<Task*>*> buffer_{ nullptr };
    std::vector<std::unique_ptr<RingBuffer<Task*>>> old_buffers_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopped_{ false };

    // convenience aliases
    static constexpr std::memory_order m_relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order m_acquire = std::memory_order_acquire;
    static constexpr std::memory_order m_release = std::memory_order_release;
    static constexpr std::memory_order m_seq_cst = std::memory_order_seq_cst;
};

//! Task manager based on work stealing
struct TaskManager
{
    std::vector<TaskQueue> queues_;
    size_t num_queues_;
    detail::aligned_atomic<size_t> push_idx_{ 0 };
    std::atomic<bool> stopped_{ false };
    std::atomic_size_t todo_list_{ 0 };

    explicit TaskManager(size_t num_queues)
      : queues_{ std::vector<TaskQueue>(num_queues) }
      , num_queues_{ num_queues }
    {}

    template<typename Task>
    void push(Task&& task)
    {
        if (stopped_)
            return;
        for (size_t count = 0; count < num_queues_ * 20; count++) {
            if (queues_[push_idx_++ % num_queues_].try_push(task))
                return;
        }
        queues_[push_idx_++ % num_queues_].force_push(task);
    }

    template<typename Task>
    bool try_pop(Task& task, size_t worker_id = 0)
    {
        if (stopped_)
            return false;
        for (size_t k = 0; k <= num_queues_; k++) {
            if (queues_[(worker_id + k) % num_queues_].try_pop(task))
                return true;
        }
        return false;
    }

    void wait_for_jobs(size_t id) { queues_[id].wait(); }

    void stop()
    {
        for (auto& q : queues_)
            q.stop();
        stopped_ = true;
    }

    bool stopped() { return stopped_; }
};

} // end namespace detail

//! A work stealing thread pool.
class ThreadPool
{
  public:
    //! constructs a thread pool with as many workers as there are cores.
    ThreadPool()
      : ThreadPool(std::thread::hardware_concurrency())
    {}

    //! constructs a thread pool.
    //! @param n_workers number of worker threads to create; defaults to
    //! number of available (virtual) hardware cores.
    explicit ThreadPool(size_t n_workers)
      : task_manager_{ n_workers }
    {
        for (size_t id = 0; id < n_workers; ++id) {
            workers_.emplace_back([this, id] {
                std::function<void()> task;
                while (!task_manager_.stopped()) {
                    task_manager_.wait_for_jobs(id);
                    do {
                        // inner while to save a few cash misses calling empty()
                        while (task_manager_.try_pop(task, id))
                            this->execute_safely(task);
                    } while (!todo_list_.empty());
                }
            });
        }
    }

    ~ThreadPool()
    {
        task_manager_.stop();
        for (auto& worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
    }

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;

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
    void push(Function&& f, Args&&... args)
    {
        if (workers_.size() == 0)
            return f(args...);
        todo_list_.add();
        task_manager_.push(
          std::bind(std::forward<Function>(f), std::forward<Args>(args)...));
    }

    //! @brief executes a job asynchronously the global thread pool.
    //! @param f a function.
    //! @param args (optional) arguments passed to `f`.
    //! @return A `std::future` for the task. Call `future.get()` to retrieve
    //! the results at a later point in time (blocking).
    template<class Function, class... Args>
    auto async(Function&& f, Args&&... args)
      -> std::future<decltype(f(args...))>
    {
        auto pack =
          std::bind(std::forward<Function>(f), std::forward<Args>(args)...);
        using pack_t = std::packaged_task<decltype(f(args...))()>;
        auto task_ptr = std::make_shared<pack_t>(std::move(pack));
        this->push([task_ptr] { (*task_ptr)(); });
        return task_ptr->get_future();
    }

    //! @brief waits for all jobs currently running on the global thread pool.
    void wait() { todo_list_.wait(); }

  private:
    void execute_safely(std::function<void()>& task)
    {
        try {
            task();
            todo_list_.cross();
        } catch (...) {
            todo_list_.stop(std::current_exception());
            task_manager_.stop();
        }
    }

    detail::TaskManager task_manager_;
    TodoList todo_list_{ 0 };
    std::vector<std::thread> workers_;
};

//! Direct access to the global thread pool -------------------

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
inline void
wait()
{
    ThreadPool::global_instance().wait();
}

} // end namespace quickpool
