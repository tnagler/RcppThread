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
    , buffer_(TaskVec(capacity))
  {
    if (capacity_ && (!(capacity_ & (capacity_ - 1))))
      throw std::runtime_error("capacity must be a power of two");
  }

  size_t capacity() const { return capacity_; }

  // Store (copy) at modulo index
  void store(size_t i, Task&& task) { buffer_[i & mask_] = std::move(task); }

  // Load (copy) at modulo index
  Task load(size_t i) const { return buffer_[i & mask_]; }

  // Allocates and returns a new ring buffer and copies current elements.
  RingBuffer enlarge(size_t bottom, size_t top) const
  {
    RingBuffer buffer(2 * capacity_);
    for (size_t i = top; i != bottom; ++i)
      buffer.store(i, this->load(i));
    return buffer;
  }

private:
  size_t capacity_; // Capacity of the buffer
  size_t mask_;     // Bit mask to perform modulo capacity operations
  TaskVec buffer_;
};

} // end namespace RcppThread::detail

class TaskQueue
{
  using Task = std::function<void()>;

public:
  //! constructs the que with a given capacity.
  //! @param capacity must be a power of two.
  TaskQueue(size_t capacity = 1024);

  // move/copy is not supported
  TaskQueue(TaskQueue const& other) = delete;
  TaskQueue& operator=(TaskQueue const& other) = delete;

  //! queries the size.
  size_t size() const;

  //! queries the capacity.
  size_t capacity() const;

  //! checks if queue is empty.
  bool empty() const;

  //! pushes a task to the bottom of the queue; enlarges the queue if full.
  void push(Task&& task);

  //! pops a task from the bottom of the queue. Only the owner thread should
  //! pop.
  Task pop();

  //! steals an item from the top of the queue.
  Task steal();

  //! destructs the queue.
  ~TaskQueue();

private:
  alignas(64) std::atomic_size_t top_{0};
  alignas(64) std::atomic_size_t bottom_{0};
  alignas(64) std::vector<detail::RingBuffer> buffers_;
  alignas(64) std::atomic_size_t bufferIndex_{0};

  // convenience aliases
  static constexpr std::memory_order m_relaxed = std::memory_order_relaxed;
  static constexpr std::memory_order m_consume = std::memory_order_consume;
  static constexpr std::memory_order m_acquire = std::memory_order_acquire;
  static constexpr std::memory_order m_release = std::memory_order_release;
  static constexpr std::memory_order m_seq_cst = std::memory_order_seq_cst;
};



} // end namespace RcppThread
