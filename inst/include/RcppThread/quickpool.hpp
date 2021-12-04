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

// Layout of quickpool.hpp
//
// 1. Memory related utilities.
//    - Memory order aliases
//    - Class for padding bytes
//    - Class for cache aligned atomics
//    - Class for load/assign atomics with relaxed order
// 2. Loop related utilities.
//    - Worker class for parallel for loops
// 3. Scheduling utilities.
//    - Ring buffer
//    - Task queue
//    - Task manager
// 4. Thread pool class
// 5. Free-standing functions (main API)

//! quickpool namespace
namespace quickpool {

// 1. --------------------------------------------------------------------------

//! Memory related utilities.
namespace mem {

//! convenience definitions
static constexpr std::memory_order relaxed = std::memory_order_relaxed;
static constexpr std::memory_order acquire = std::memory_order_acquire;
static constexpr std::memory_order release = std::memory_order_release;
static constexpr std::memory_order seq_cst = std::memory_order_seq_cst;

//! Padding char[]s always must hold at least one char. If the size of the
//! object ends at an alignment point, we don't want to pad one extra byte
//! however. The construct below ensures that padding bytes are only added if
//! necessary.
namespace padding_impl {

//! constexpr modulo operator.
constexpr size_t
mod(size_t a, size_t b)
{
    return a - b * (a / b);
}

// Padding bytes from end of aligned object until next alignment point. char[]
// must hold at least one byte.
template<class T, size_t Align>
struct padding_bytes
{
    static constexpr size_t free_space =
      Align - mod(sizeof(std::atomic<T>), Align);
    static constexpr size_t required = free_space > 1 ? free_space : 1;
    char padding_[required];
};

struct empty_struct
{};

//! Class holding padding bytes is necessary. Classes can inherit from this
//! to automically add padding if necessary.
template<class T, size_t Align>
struct padding
  : std::conditional<mod(sizeof(std::atomic<T>), Align) != 0,
                     padding_bytes<T, Align>,
                     empty_struct>::type
{};

} // end namespace padding_impl

//! Memory-aligned atomic `std::atomic<T>`. Behaves like `std::atomic<T>`, but
//! overloads operators `new` and `delete` to align its memory location. Padding
//! bytes are added if necessary.
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
    T operator=(T x) noexcept { return std::atomic<T>::operator=(x); }
    T operator=(T x) volatile noexcept { return std::atomic<T>::operator=(x); }

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
            // Read pointer to start of malloc()'ed block and free there.
            std::free(*(static_cast<void**>(ptr) - 1));
        }
    }
};

//! Fast and simple load/assign atomic with no memory ordering guarantees.
template<typename T>
struct relaxed_atomic : public aligned_atomic<T>
{
    explicit relaxed_atomic(T value)
      : mem::aligned_atomic<T>(value)
    {}

    operator T() const noexcept { return this->load(mem::relaxed); }

    T operator=(T desired) noexcept
    {
        this->store(desired, mem::relaxed);
        return desired;
    }
};

} // end namespace mem

// 2. --------------------------------------------------------------------------

//! Loop related utilities.
namespace loop {

//! Worker state.
struct State
{
    int pos; //!< position in the loop range
    int end; //!< end of range assigned to worker
};

//! Worker class for parallel loops.
//!
//! When a worker completes its own range, it steals half of the remaining range
//! of another worker. The number of steals (= only source of contention) is
//! therefore only logarithmic in the number of tasks. The algorithm uses
//! double-width compare-and-swap, which is lock-free on most modern processor
//! architectures.
//!
//! @tparam type of function processing the loop (required to account for
//! type-erasure).
template<typename Function>
struct Worker
{
    Worker() {}
    Worker(int begin, int end, Function fun)
      : state{ State{ begin, end } }
      , f{ fun }
    {}

    Worker(Worker&& other)
      : state{ other.state.load() }
      , f{ std::forward<Function>(other.f) }
    {}

    size_t tasks_left() const
    {
        State s = state;
        return s.end - s.pos;
    }

    bool done() const { return (tasks_left() == 0); }

    //! @param others pointer to the vector of all workers.
    void run(std::shared_ptr<std::vector<Worker>> others)
    {
        State s, s_old; // temporary state variables
        do {
            s = state;
            if (s.pos < s.end) {
                // Protect slot by trying to advance position before doing work.
                s_old = s;
                s.pos++;

                // Another worker might have changed the end of the range in
                // the meanwhile. Check atomically if the state is unaltered
                // and, if so, replace by advanced state.
                if (state.compare_exchange_weak(s_old, s)) {
                    f(s_old.pos); // succeeded, do work
                } else {
                    continue; // failed, try again
                }
            }
            if (s.pos == s.end) {
                // Reached end of own range, steal range from others. Range
                // remains empty if all work is done, so we can leave the loop.
                this->steal_range(*others);
            }
        } while (!this->done());
    }

    //! @param workers vector of all workers.
    void steal_range(std::vector<Worker>& workers)
    {
        do {
            Worker& other = find_victim(workers);
            State s = other.state;
            if (s.pos >= s.end - 1) {
                continue; // other range is empty by now
            }

            // Remove second half of the range. Check atomically if the state is
            // unaltered and, if so, replace with reduced range.
            auto s_old = s;
            s.end -= (s.end - s.pos + 1) / 2;
            if (other.state.compare_exchange_weak(s_old, s)) {
                // succeeded, update own range
                state = State{ s.end, s_old.end };
                break;
            }
        } while (!all_done(workers)); // failed steal, try again
    }

    //! @param workers vector of all workers.
    bool all_done(const std::vector<Worker>& workers)
    {
        for (const auto& worker : workers) {
            if (!worker.done())
                return false;
        }
        return true;
    }

    //! targets the worker with the largest remaining range to minimize number
    //! of steal events.
    //! @param others vector of all workers.
    Worker& find_victim(std::vector<Worker>& workers)
    {
        std::vector<size_t> tasks_left;
        tasks_left.reserve(workers.size());
        for (const auto& worker : workers) {
            tasks_left.push_back(worker.tasks_left());
        }
        auto max_it = std::max_element(tasks_left.begin(), tasks_left.end());
        auto idx = std::distance(tasks_left.begin(), max_it);
        return workers[idx];
    }

    mem::relaxed_atomic<State> state; //!< worker state `{pos, end}`
    Function f;                       //< function applied to the loop index
};

//! creates loop workers. They must be passed to each worker using a shared
//! pointer, so that they persist if an inner `parallel_for()` in a nested
//! loop exits.
template<typename Function>
std::shared_ptr<std::vector<Worker<Function>>>
create_workers(const Function& f, int begin, int end, size_t num_workers)
{
    auto num_tasks = std::max(end - begin, static_cast<int>(0));
    auto workers = new std::vector<Worker<Function>>;
    workers->reserve(num_workers);
    for (size_t i = 0; i < num_workers; i++) {
        workers->emplace_back(begin + num_tasks * i / num_workers,
                              begin + num_tasks * (i + 1) / num_workers,
                              f);
    }
    return std::shared_ptr<std::vector<Worker<Function>>>(std::move(workers));
}

} // end namespace loop

// 3. -------------------------------------------------------------------------

//! Task management utilities.
namespace sched {

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
    using Task = std::function<void()>;

  public:
    //! @param capacity must be a power of two.
    TaskQueue(size_t capacity = 256)
      : buffer_{ new RingBuffer<Task*>(capacity) }
    {}

    ~TaskQueue() noexcept
    {
        // Must free memory allocated by push(), but not freed by try_pop().
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
    void push(Task&& task)
    {
        // Must hold lock in case of multiple producers.
        std::unique_lock<std::mutex> lk(mutex_);

        auto b = bottom_.load(mem::relaxed);
        auto t = top_.load(mem::acquire);
        RingBuffer<Task*>* buf_ptr = buffer_.load(mem::relaxed);

        if (static_cast<int>(buf_ptr->capacity()) < (b - t) + 1) {
            // Buffer is full, create enlarged copy before continuing.
            auto old_buf = buf_ptr;
            buf_ptr = std::move(buf_ptr->enlarged_copy(b, t));
            old_buffers_.emplace_back(old_buf);
            buffer_.store(buf_ptr, mem::relaxed);
        }

        //! Store pointer to new task in ring buffer.
        buf_ptr->set_entry(b, new Task{ std::forward<Task>(task) });
        bottom_.store(b + 1, mem::release);

        lk.unlock(); // can release before signal
        cv_.notify_one();
    }

    //! pops a task from the top of the queue; returns false if lost race.
    bool try_pop(Task& task)
    {
        auto t = top_.load(mem::acquire);
        std::atomic_thread_fence(mem::seq_cst);
        auto b = bottom_.load(mem::acquire);

        if (t < b) {
            // Must load task pointer before acquiring the slot, because it
            // could be overwritten immediately after.
            auto task_ptr = buffer_.load(mem::acquire)->get_entry(t);

            // Atomically try to advance top.
            if (top_.compare_exchange_strong(
                  t, t + 1, mem::seq_cst, mem::relaxed)) {
                task = std::move(*task_ptr); // won race, get task
                delete task_ptr;             // fre memory allocated in push()
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
        cv_.notify_one();
    }

    void wake_up()
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
        }
        cv_.notify_one();
    }

  private:
    //! queue indices
    mem::aligned_atomic<int> top_{ 0 };
    mem::aligned_atomic<int> bottom_{ 0 };

    //! ring buffer holding task pointers
    std::atomic<RingBuffer<Task*>*> buffer_{ nullptr };

    //! pointers to buffers that were replaced by enlarged buffer
    std::vector<std::unique_ptr<RingBuffer<Task*>>> old_buffers_;

    //! synchronization variables
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_{ false };
};

//! Task manager based on work stealing.
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
        rethrow_exception(); // push() throws if a task has errored.
        if (is_running()) {
            todo_.fetch_add(1, mem::release);
            queues_[push_idx_++ % num_queues_].push(task);
        }
    }

    template<typename Task>
    bool try_pop(Task& task, size_t worker_id = 0)
    {
        // Always start pop cycle at own queue to avoid contention.
        for (size_t k = 0; k <= num_queues_; k++) {
            if (queues_[(worker_id + k) % num_queues_].try_pop(task)) {
                if (is_running()) {
                    return true;
                } else {
                    // Throw away task if pool has stopped or errored.
                    return false;
                }
            }
        }

        return false;
    }

    void wake_up_all_workers()
    {
        for (auto& q : queues_)
            q.wake_up();
    }

    void wait_for_jobs(size_t id)
    {
        if (has_errored()) {
            // Main thread may be waiting to reset the pool.
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
        if (is_running()) {
            auto wake_up = [this] { return (todo_ <= 0) || !is_running(); };
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
        auto n = todo_.fetch_sub(1, mem::release) - 1;
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
        std::lock_guard<std::mutex> lk(mtx_);
        if (has_errored()) // only catch first exception
            return;
        err_ptr_ = err_ptr;
        status_ = Status::errored;

        // Some threads may change todo_ after we stop. The large
        // negative number forces them to exit the processing loop.
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
        // Exceptions are only thrown from the owner thread, not in workers.
        if (called_from_owner_thread() && has_errored()) {
            {
                // Wait for all threads to idle so we can clean up after them.
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this] { return num_waiting_ == num_queues_; });
            }
            // Before throwing: restore defaults for potential future use of
            // the task manager.
            todo_ = 0;
            auto current_exception = err_ptr_;
            err_ptr_ = nullptr;
            status_ = Status::running;

            std::rethrow_exception(current_exception);
        }
    }

    bool is_running() const
    {
        return status_.load(mem::relaxed) == Status::running;
    }

    bool has_errored() const
    {
        return status_.load(mem::relaxed) == Status::errored;
    }

    bool stopped() const
    {
        return status_.load(mem::relaxed) == Status::stopped;
    }

    bool done() const
    {
        if (todo_.load(mem::relaxed) <= 0)
            return true;
        for (auto& q : queues_) {
            if (!q.empty())
                return false;
        }
        return true;
    }

  private:
    //! worker queues
    std::vector<TaskQueue> queues_;
    size_t num_queues_;

    //! task management
    mem::relaxed_atomic<size_t> num_waiting_{ 0 };
    mem::relaxed_atomic<size_t> push_idx_{ 0 };
    mem::aligned_atomic<int> todo_{ 0 };

    //! synchronization variables
    const std::thread::id owner_id_;
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

} // end namespace sched

// 4. ------------------------------------------------------------------------

//! A work stealing thread pool.
class ThreadPool
{
  public:
    //! @brief constructs a thread pool with as many workers as there are cores.
    ThreadPool()
      : ThreadPool(std::thread::hardware_concurrency())
    {}

    //! @brief constructs a thread pool.
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
                        // inner while to save some time calling done()
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

    //! @brief executes a job asynchronously on the global thread pool.
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

    //! @brief computes an index-based parallel for loop.
    //!
    //! Waits until all tasks have finished, unless called from a thread that
    //! didn't create the pool. If this is taken into account, parallel loops
    //! can be nested.
    //!
    //! @param begin first index of the loop.
    //! @param end the loop runs in the range `[begin, end)`.
    //! @param f a function taking `int` argument (the 'loop body').
    //! @param nthreads optional; limits the number of threads.
    template<class UnaryFunction>
    void parallel_for(int begin,
                      int end,
                      UnaryFunction f,
                      size_t nthreads = std::thread::hardware_concurrency())
    {
        // each worker has its dedicated range, but can steal part of another
        // worker's ranges when done with own
        nthreads = std::min(end - begin, static_cast<int>(nthreads));
        nthreads = std::min(nthreads, workers_.size());
        auto workers =
          loop::create_workers<UnaryFunction>(f, begin, end, nthreads);
        for (int k = 0; k < nthreads; k++) {
            this->push([=] { workers->at(k).run(workers); });
        }
        this->wait();
    }

    //! @brief computes a iterator-based parallel for loop.
    //!
    //! Waits until all tasks have finished, unless called from a thread that
    //! didn't create the pool. If this is taken into account, parallel loops
    //! can be nested.
    //!
    //! @param items an object allowing for `std::begin()` and `std::end()`.
    //! @param f function to be applied as `f(*it)` for the iterator in the
    //! range `[begin, end)` (the 'loop body').
    //! @param nthreads optional; limits the number of threads.
    template<class Items, class UnaryFunction>
    inline void parallel_for_each(
      Items& items,
      UnaryFunction&& f,
      size_t nthreads = std::thread::hardware_concurrency())
    {
        auto begin = std::begin(items);
        auto size = std::distance(begin, std::end(items));
        this->parallel_for(0, size, [=](int i) { f(begin[i]); }, nthreads);
    }

    //! @brief waits for all jobs currently running on the global thread pool.
    //! @param millis if > 0: stops waiting after millis ms.
    void wait(size_t millis = 0) { task_manager_.wait_for_finish(millis); }

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

    sched::TaskManager task_manager_;
    std::vector<std::thread> workers_;
};

// 5. ---------------------------------------------------

//! Free-standing functions (main API)

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
//! @return A `std::future` for the task. Call `future.get()` to retrieve
//! the results at a later point in time (blocking).
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

//! @brief computes an index-based parallel for loop.
//!
//! Waits until all tasks have finished, unless called from a thread that
//! didn't create the pool. If this is taken into account, parallel loops
//! can be nested.
//!
//! @param begin first index of the loop.
//! @param end the loop runs in the range `[begin, end)`.
//! @param f a function taking `int` argument (the 'loop body').
//! @param nthreads optional; limits the number of threads.
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

//! @brief computes a iterator-based parallel for loop.
//!
//! Waits until all tasks have finished, unless called from a thread that
//! didn't create the pool. If this is taken into account, parallel loops
//! can be nested.
//!
//! @param items an object allowing for `std::begin()` and `std::end()`.
//! @param f function to be applied as `f(*it)` for the iterator in the
//! range `[begin, end)` (the 'loop body').
//! @param nthreads optional; limits the number of threads.
template<class Items, class UnaryFunction>
inline void
parallel_for_each(Items& items,
                  UnaryFunction&& f,
                  size_t nthreads = std::thread::hardware_concurrency())
{
    ThreadPool::global_instance().parallel_for_each(items, f, nthreads);
}

} // end namespace quickpool
