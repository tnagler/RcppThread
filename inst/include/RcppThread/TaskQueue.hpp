// Copyright © 2021 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace RcppThread {

namespace detail {

//! A simple ring buffer class.
class RingBuffer
{
  using Task = std::function<void()>;
  using TaskVec = std::vector<Task>;

public:
  explicit RingBuffer(size_t capacity)
    : capacity_(capacity)
    , mask_(capacity - 1)
  {
    if (capacity_ && (!(capacity_ & (capacity_ - 1))))
      throw std::runtime_error("capacity must be a power of two");
    buffer_ = std::unique_ptr<TaskVec>(new TaskVec(capacity));
  }

  size_t capacity() const { return capacity_; }

  // Store (copy) at modulo index
  void store(size_t i, Task&& task) { (*buffer_)[i & mask_] = std::move(task); }

  // Load (copy) at modulo index
  Task load(std::size_t i) const { return (*buffer_)[i & mask_]; }

  // Allocates and returns a new ring buffer and copies current elements.
  std::unique_ptr<RingBuffer> enlarge(size_t bottom, size_t top) const
  {
    std::unique_ptr<RingBuffer> ptr; // (new RingBuffer(2 * capacity_));
    for (size_t i = top; i != bottom; ++i)
      ptr->store(i, this->load(i));
    return ptr;
  }

private:
  size_t capacity_; // Capacity of the buffer
  size_t mask_;     // Bit mask to perform modulo capacity operations
  std::unique_ptr<TaskVec> buffer_;
};

} // end namespace RcppThread::detail


class TaskQueue
{
  using Task = std::function<void()>;
  
public:
  //! constructs the que with a given capacity.
  //! @param capacity must be a power of two.
  TaskQueue(size_t capacity = 1024);

  // move/Copy is not supported
  TaskQueue(TaskQueue const& other) = delete;
  TaskQueue& operator=(TaskQueue const& other) = delete;

  //! queries the size.
  std::size_t size() const;

  //! queries the capacity.
  int64_t capacity() const;

  //! checks if queue is empty.
  bool empty() const;

  //! pushes a task to the bottom of the queue; enlarges the queue if full.
  void push(Task&&);

  //! pops a task from the bottom of the queue. Only the owner thread should
  //! pop.
  Task pop();

  //! steals an item from the top of the queue.
  Task steal();

  //! destructs the queue.
  ~TaskQueue();

private:
  alignas(64) std::atomic_size_t top_;
  alignas(64) std::atomic_size_t bottom_;
  alignas(64) detail::RingBuffer buffer_;

  std::unique_ptr<detail::RingBuffer> garbage_; // used while enlarging

  // convenience aliases
  static constexpr std::memory_order mem_relaxed = std::memory_order_relaxed;
  static constexpr std::memory_order mem_consume = std::memory_order_consume;
  static constexpr std::memory_order mem_acquire = std::memory_order_acquire;
  static constexpr std::memory_order mem_release = std::memory_order_release;
  static constexpr std::memory_order mem_seq_cst = std::memory_order_seq_cst;
};


} // end namespace RcppThread
