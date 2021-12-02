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

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

//! quickpool namespace
namespace quickpool {

namespace mem {

//! convenience definitions
static constexpr std::memory_order relaxed = std::memory_order_relaxed;
static constexpr std::memory_order acquire = std::memory_order_acquire;
static constexpr std::memory_order release = std::memory_order_release;
static constexpr std::memory_order seq_cst = std::memory_order_seq_cst;

namespace padding_impl {

// Padding char[]s always must hold at least one char. We do some template
// metaprogramming to ensure padding is only added if required.

constexpr size_t
mod(size_t a, size_t b)
{
    return a - b * (a / b);
}

template<class T, size_t Align>
struct padding_bytes
{
    static constexpr size_t free_space =
      Align - mod(sizeof(std::atomic<T>), Align);
    static constexpr size_t padding_size_ = free_space > 1 ? free_space : 1;
    char padding_[padding_size_ + sizeof(void*)];
};

struct empty_struct
{};

template<class T, size_t Align>
struct padding
  : std::conditional<mod(sizeof(std::atomic<T>), Align) != 0,
                     padding_bytes<T, Align>,
                     empty_struct>::type
{};

} // end namespace padding_impl

template<class T, size_t Align = 64>
struct alignas(Align) aligned_atomic
  : public std::atomic<T>
  , private padding_impl::padding<T, Align>
{
  public:
    aligned_atomic() noexcept = default;

    aligned_atomic(T desired) noexcept
      : std::atomic<T>(desired)
    {}

    // Assignment operators have been deleted, must redefine.
    T operator=(T desired) noexcept
    {
        this->store(desired);
        return desired;
    }

    T operator=(T desired) volatile noexcept
    {
        this->store(desired);
        return desired;
    }

    // The alloc/dealloc mechanism is pretty much
    // https://www.boost.org/doc/libs/1_76_0/boost/align/detail/aligned_alloc.hpp

    static void* operator new(size_t count) noexcept
    {
        // Make sure alignment is at least that of void*.
        constexpr size_t alignment =
          (Align >= alignof(void*)) ? Align : alignof(void*);

        // Allocate enough space required for object and a void*.
        size_t space = count + alignment + sizeof(void*);
        void* p = std::malloc(space);
        if (p == nullptr) {
            return nullptr;
        }

        // Shift pointer to leave space for void*.
        void* p_algn = static_cast<char*>(p) + sizeof(void*);
        space -= sizeof(void*);

        // Shift pointer further to ensure proper alignment.
        (void)std::align(alignment, count, p_algn, space);

        // Store unaligned pointer with offset sizeof(void*) before aligned
        // location. Later we'll know where to look for the pointer telling
        // us where to free what we malloc()'ed above.
        *(static_cast<void**>(p_algn) - 1) = p;

        return p_algn;
    }

    static void operator delete(void* ptr)
    {
        if (ptr) {
            std::free(*(static_cast<void**>(ptr) - 1));
        }
    }
};

// fast atomic with no memory ordering guarantees
template<typename T>
struct relaxed_atomic : public aligned_atomic<T>
{
    relaxed_atomic(T value)
      : mem::aligned_atomic<T>(value)
    {}

    operator T() const noexcept { return this->load(mem::relaxed); }

    T operator=(T desired) noexcept
    {
        this->store(desired, mem::relaxed);
        return desired;
    }
};

}

namespace loop {

struct Range
{
    Range(int begin, int end)
      : pos{ begin }
      , end{ end }
    {}

    Range(const Range& other)
      : pos{ other.pos.load() }
      , end{ other.end.load() }
    {}

    bool empty() const { return end - pos < 1; }
    int size() const { return end - pos; }

    bool try_local_work(int& p)
    {
        p = pos + 1;
        pos = p;
        std::atomic_thread_fence(mem::seq_cst);
        if (p < end) {
            p = p - 1;
            return true;
        }
        // conflict detected: rollback under lock
        pos = p - 1;
        {
            std::lock_guard<std::mutex> lk(mutex);
            p = pos;
            if (p < end)
                pos = p + 1;
        }

        if (p < pos)
            return true;
        return false;
    }

    bool try_steal_range(Range& other_range)
    {
        if (this == &other_range)
            return false;
        if (other_range.end < other_range.pos)
            return false;

        int e, chunk_size;
        {
            std::unique_lock<std::mutex> lk_other(other_range.mutex,
                                                  std::try_to_lock);
            if (!lk_other) {
                return false;
            }
            e = other_range.end;
            chunk_size = (e - other_range.pos) / 2;
            e = e - chunk_size;
            other_range.end = e;
            std::atomic_thread_fence(std::memory_order_acq_rel);
            if (e <= other_range.pos) {
                // rollback and abort
                other_range.end = e + chunk_size;
                return false;
            }
        }

        std::lock_guard<std::mutex> lk_self(mutex);
        pos = e;
        end = e + chunk_size;

        return true;
    }

    mem::relaxed_atomic<int> pos;
    mem::relaxed_atomic<int> end;
    std::mutex mutex;
};

class Ranges
{
  public:
    Ranges(int begin, int end, size_t num_ranges)
    {
        auto num_tasks = end - begin;
        ranges_.reserve(num_ranges);
        for (size_t i = 0; i < num_ranges; i++) {
            ranges_.emplace_back(begin + num_tasks * i / num_ranges,
                                 begin + num_tasks * (i + 1) / num_ranges);
        }
    }

    Range& operator[](size_t index) { return ranges_[index]; }

    bool seems_empty() const
    {
        for (const auto& range : ranges_) {
            if (!range.empty())
                return false;
        }
        return true;
    }

    std::vector<size_t> range_order() const
    {
        std::vector<size_t> range_sizes;
        range_sizes.reserve(ranges_.size());
        for (const auto& range : ranges_)
            range_sizes.push_back(range.size());

        std::vector<size_t> order(ranges_.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t i, size_t j) {
            return range_sizes[i] > range_sizes[j];
        });

        return order;
    }

    void distribute_to(Range& range)
    {
        const auto n = ranges_.size();
        const auto order = range_order();
        size_t idx = 0;
        while (!seems_empty()) {
            if (range.try_steal_range(ranges_[order[idx++ % n]]))
                break;
        }
    }

  private:
    std::vector<Range> ranges_;
};

} // end namespace loop

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

//! A multi-producer, multi-consumer queue; pops are lock free.
class TaskQueue
{
    // convenience aliases
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
        for (int i = top_; i < bottom_.load(mem::relaxed); ++i)
            delete buf_ptr->get_entry(i);
        delete buf_ptr;
    }

    TaskQueue(TaskQueue const& other) = delete;
    TaskQueue& operator=(TaskQueue const& other) = delete;

    //! checks if queue is empty.
    bool empty() const
    {
        return (bottom_.load(mem::relaxed) <= top_.load(mem::relaxed));
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
            auto b = bottom_.load(mem::relaxed);
            auto t = top_.load(mem::acquire);
            RingBuffer<Task*>* buf_ptr = buffer_.load(mem::relaxed);

            if (static_cast<int>(buf_ptr->capacity()) < (b - t) + 1) {
                // buffer is full, create enlarged copy before continuing
                auto old_buf = buf_ptr;
                buf_ptr = std::move(buf_ptr->enlarged_copy(b, t));
                old_buffers_.emplace_back(old_buf);
                buffer_.store(buf_ptr, mem::relaxed);
            }

            buf_ptr->set_entry(b, new Task{ std::forward<Task>(task) });
            bottom_.store(b + 1, mem::release);
        }
        cv_.notify_one();
        return true;
    }

    //! pops a task from the top of the queue; returns false if lost race.
    bool try_pop(Task& task)
    {
        auto t = top_.load(mem::acquire);
        std::atomic_thread_fence(mem::seq_cst);
        auto b = bottom_.load(mem::acquire);

        if (t < b) {
            // must load task pointer before acquiring the slot, because it
            // could be overwritten immediately after
            auto task_ptr = buffer_.load(mem::acquire)->get_entry(t);

            if (top_.compare_exchange_strong(
                  t, t + 1, mem::seq_cst, mem::relaxed)) {
                task = std::move(*task_ptr); // won race, get task
                delete task_ptr; // fre memory allocated in push_unsafe()
                return true;
            }
        }
        return false; // queue is empty or lost race
    }

    //! waits for tasks or stop signal.
    void wait()
    {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return !this->empty() || stopped_; });
    }

    //! stops the queue and wakes up all workers waiting for jobs.
    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

  private:
    mem::aligned_atomic<int> top_{ 0 };
    mem::aligned_atomic<int> bottom_{ 0 };
    std::atomic<RingBuffer<Task*>*> buffer_{ nullptr };
    std::vector<std::unique_ptr<RingBuffer<Task*>>> old_buffers_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_{ false };
};

//! Task manager based on work stealing
class TaskManager
{
  public:
    explicit TaskManager(size_t num_queues)
      : queues_{ std::vector<TaskQueue>(num_queues) }
      , num_queues_{ num_queues }
      , owner_id_{ std::this_thread::get_id() }
    {}

    template<typename Task>
    void push(Task&& task)
    {
        rethrow_exception();
        todo_.fetch_add(1, mem::relaxed);

        size_t q_idx;
        while (running()) {
            q_idx = push_idx_.fetch_add(1, mem::relaxed) % num_queues_;
            if (queues_[q_idx].try_push(task))
                return;
        }
    }

    template<typename Task>
    bool try_pop(Task& task, size_t worker_id = 0)
    {
        for (size_t k = 0; k <= num_queues_; k++) {
            if (queues_[(worker_id + k) % num_queues_].try_pop(task)) {
                if (running())
                    return true;
            }
        }
        return false;
    }

    void wait_for_jobs(size_t id)
    {
        if (errored()) {
            // main thread may be waiting to reset the pool
            std::lock_guard<std::mutex> lk(mtx_);
            if (++num_waiting_ == num_queues_)
                cv_.notify_all();
        } else {
            ++num_waiting_;
        }

        queues_[id].wait();
        --num_waiting_;
    }

    //! @param millis if > 0: stops waiting after millis ms
    void wait_for_finish(size_t millis = 0)
    {
        if (running()) {
            auto wake_up = [this] { return (todo_ <= 0) || !running(); };
            std::unique_lock<std::mutex> lk(mtx_);
            if (millis == 0) {
                cv_.wait(lk, wake_up);
            } else {
                cv_.wait_for(lk, std::chrono::milliseconds(millis), wake_up);
            }
        }
        rethrow_exception();
    }

    bool called_from_owner_thread() const
    {
        return (std::this_thread::get_id() == owner_id_);
    }

    void report_success()
    {
        auto n = todo_.fetch_sub(1, mem::relaxed) - 1;
        if (n <= 0) {
            // all jobs are done; lock before signal to prevent spurious failure
            {
                std::lock_guard<std::mutex> lk{ mtx_ };
            }
            cv_.notify_all();
        }
    }

    void report_fail(std::exception_ptr err_ptr)
    {
        if (errored()) // only catch first exception
            return;

        std::lock_guard<std::mutex> lk(mtx_);
        if (errored()) // double check under lock
            return;

        // Some threads may add() or cross() after we stop. The large
        // negative number prevents num_tasks_ from becoming positive
        // again.
        err_ptr_ = err_ptr;
        status_ = Status::errored;
        todo_.store(std::numeric_limits<int>::min() / 2);

        cv_.notify_all();
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            status_ = Status::stopped;
        }
        // Worker threads wait on queue-specific mutex -> notify all queues.
        for (auto& q : queues_)
            q.stop();
    }

    void rethrow_exception()
    {
        if (called_from_owner_thread() && errored()) {
            // wait for all threads to idle
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return num_waiting_ == num_queues_; });
            lk.unlock();

            // before throwing: restore defaults for potential future use
            todo_ = 0;
            auto current_exception = err_ptr_;
            err_ptr_ = nullptr;
            status_ = Status::running;
            std::rethrow_exception(current_exception);
        }
    }

    bool running() const
    {
        return status_.load(mem::relaxed) == Status::running;
    }

    bool errored() const
    {
        return status_.load(mem::relaxed) == Status::errored;
    }

    bool stopped() const
    {
        return status_.load(mem::relaxed) == Status::stopped;
    }

    bool done() const { return (todo_.load(mem::relaxed) <= 0); }

  private:
    std::vector<TaskQueue> queues_;
    size_t num_queues_;
    const std::thread::id owner_id_;

    // task management
    mem::aligned_atomic<size_t> num_waiting_{ 0 };
    mem::aligned_atomic<size_t> push_idx_{ 0 };
    mem::aligned_atomic<int> todo_{ 0 };

    // synchronization in case of error to make pool reusable
    enum class Status
    {
        running,
        errored,
        stopped
    };
    mem::aligned_atomic<Status> status_{ Status::running };
    std::mutex mtx_;
    std::condition_variable cv_;
    std::exception_ptr err_ptr_{ nullptr };
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
                        // inner while to save cash misses when calling done()
                        while (task_manager_.try_pop(task, id))
                            this->execute_safely(task);
                    } while (!task_manager_.done());
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

    //! @brief pushes a job to the thread pool.
    //! @param f a function.
    //! @param args (optional) arguments passed to `f`.
    template<class Function, class... Args>
    void push(Function&& f, Args&&... args)
    {
        if (workers_.size() == 0)
            return f(args...);
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

    template<class Function>
    void parallel_for(int begin,
                      int end,
                      Function&& f,
                      size_t nthreads = std::thread::hardware_concurrency())
    {
        if (end <= begin)
            return;
        nthreads = std::min(end - begin, static_cast<int>(nthreads));

        // each worker has its dedicated range, but can steal part of another
        // worker's ranges when done with own
        auto ranges = std::make_shared<loop::Ranges>(begin, end, nthreads);
        const auto run_worker = [=](int id) {
            do {
                int pos;
                while ((*ranges)[id].try_local_work(pos)) {
                    f(pos);
                };
                ranges->distribute_to((*ranges)[id]);
            } while (!(*ranges)[id].empty());
        };

        for (int k = 0; k < nthreads; k++)
            this->push(run_worker, k);
        this->wait();
    }

    template<class Function, class Items>
    void parallel_for_each(Items& items, Function&& f)
    {
        this->parallel_for(
          0, items.size(), [&items, &f](size_t i) { f(items[i]); });
        this->wait();
    }

    //! @brief waits for all jobs currently running on the global thread pool.
    void wait() { task_manager_.wait_for_finish(); }

  private:
    void execute_safely(std::function<void()>& task)
    {
        try {
            task();
            task_manager_.report_success();
        } catch (...) {
            task_manager_.report_fail(std::current_exception());
        }
    }

    detail::TaskManager task_manager_;
    std::vector<std::thread> workers_;
};

//! Direct access to the global thread pool -------------------

//! @brief push a job to the global thread pool.
//! @param f a function.
//! @param args (optional) arguments passed to `f`.
template<class Function, class... Args>
inline void
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
inline auto
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

template<class Function>
inline void
parallel_for(int begin,
             int end,
             Function&& f,
             size_t nthreads = std::thread::hardware_concurrency())
{
    ThreadPool::global_instance().parallel_for(
      begin, end, std::forward<Function>(f), nthreads);
}

template<class Function, class Items>
inline void
parallel_for_each(Items& items, Function&& f)
{
    ThreadPool::global_instance().parallel_for_each(items, f);
}

} // end namespace quickpool
