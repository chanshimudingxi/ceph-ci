// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2018 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation. See file COPYING.
 *
 */

#ifndef CEPH_ASYNC_DETAIL_SHARED_MUTEX_H
#define CEPH_ASYNC_DETAIL_SHARED_MUTEX_H

#include <condition_variable>
#include <mutex>

#include <boost/intrusive/list.hpp>

#include "common/async/bind_handler.h"
#include "common/async/completion.h"

namespace ceph::async::detail {

// SharedMutex impl contains everything that doesn't depend on the Executor type
class SharedMutexImpl {
 public:
  // async callback signature
  using Signature = void(boost::system::error_code);

  template <typename Executor1, typename Handler>
  void start_lock(const Executor1& ex1, Handler&& handler);
  void lock();
  void lock(boost::system::error_code& ec);
  bool try_lock();
  void unlock();

  template <typename Executor1, typename Handler>
  void start_lock_shared(const Executor1& ex1, Handler&& handler);
  void lock_shared();
  void lock_shared(boost::system::error_code& ec);
  bool try_lock_shared();
  void unlock_shared();
  void cancel();

 private:
  enum class Type { Sync, Async };
  struct LockRequest : public boost::intrusive::list_base_hook<> {
    const Type type;
    explicit LockRequest(Type type) : type(type) {}
  };
  using RequestList = boost::intrusive::list<LockRequest>;

  RequestList shared_queue; //< requests waiting on a shared lock
  RequestList exclusive_queue; //< requests waiting on an exclusive lock

  /// lock state encodes the number of shared lockers, or 'max' for exclusive
  using LockState = uint16_t;
  static constexpr LockState Unlocked = 0;
  static constexpr LockState Exclusive = std::numeric_limits<LockState>::max();
  static constexpr LockState MaxShared = Exclusive - 1;
  LockState state = Unlocked; //< current lock state

  std::mutex mutex; //< protects lock state and wait queues

  // sync requests live on the stack and wait on a condition variable
  struct SyncRequest : public LockRequest {
    std::condition_variable cond;
    std::optional<boost::system::error_code> ec;
    SyncRequest() : LockRequest(Type::Sync) {}
  };
  // async requests use async::Completion to invoke a handler on its executor
  using AsyncRequest = Completion<Signature, AsBase<LockRequest>>;

  static void complete(LockRequest *request, boost::system::error_code ec);
  static void complete(RequestList&& requests, boost::system::error_code ec);
};


template <typename Executor1, typename Handler>
void SharedMutexImpl::start_lock(const Executor1& ex1, Handler&& handler)
{
  std::lock_guard lock{mutex};

  if (state == Unlocked) {
    state = Exclusive;

    // post the completion
    auto ex2 = boost::asio::get_associated_executor(handler, ex1);
    auto alloc2 = boost::asio::get_associated_allocator(handler);
    auto b = bind_handler(std::move(handler), boost::system::error_code{});
    ex2.post(std::move(b), alloc2);
  } else {
    // create a request and add it to the exclusive list
    auto request = AsyncRequest::create(ex1, std::move(handler), Type::Async);
    exclusive_queue.push_back(*request.release());
  }
}

inline bool SharedMutexImpl::try_lock()
{
  std::lock_guard lock{mutex};

  if (state == Unlocked) {
    state = Exclusive;
    return true;
  }
  return false;
}

void SharedMutexImpl::lock(boost::system::error_code& ec)
{
  std::unique_lock lock{mutex};

  if (state == Unlocked) {
    state = Exclusive;
    ec.clear();
  } else {
    SyncRequest request;
    exclusive_queue.push_back(request);
    request.cond.wait(lock, [&request] { return request.ec; });
    ec = *request.ec;
  }
}

inline void SharedMutexImpl::lock()
{
  boost::system::error_code ec;
  lock(ec);
  if (ec) {
    throw boost::system::system_error(ec);
  }
}

void SharedMutexImpl::unlock()
{
  RequestList granted;
  {
    std::lock_guard lock{mutex};
    assert(state == Exclusive);

    if (!exclusive_queue.empty()) {
      // grant next exclusive lock
      auto& request = exclusive_queue.front();
      exclusive_queue.pop_front();
      granted.push_back(request);
    } else {
      // grant shared locks, if any
      state = shared_queue.size();
      if (state > MaxShared) {
        state = MaxShared;
        auto end = std::next(shared_queue.begin(), MaxShared);
        granted.splice(granted.end(), shared_queue,
                       shared_queue.begin(), end, MaxShared);
      } else {
        granted.splice(granted.end(), shared_queue);
      }
    }
  }
  complete(std::move(granted), boost::system::error_code{});
}

template <typename Executor1, typename Handler>
void SharedMutexImpl::start_lock_shared(const Executor1& ex1, Handler&& handler)
{
  std::lock_guard lock{mutex};

  if (exclusive_queue.empty() && state < MaxShared) {
    state++;

    auto ex2 = boost::asio::get_associated_executor(handler, ex1);
    auto alloc2 = boost::asio::get_associated_allocator(handler);
    auto b = bind_handler(std::move(handler), boost::system::error_code{});
    ex2.post(std::move(b), alloc2);
  } else {
    auto request = AsyncRequest::create(ex1, std::move(handler), Type::Async);
    shared_queue.push_back(*request.release());
  }
}

inline bool SharedMutexImpl::try_lock_shared()
{
  std::lock_guard lock{mutex};

  if (exclusive_queue.empty() && state < MaxShared) {
    state++;
    return true;
  }
  return false;
}

void SharedMutexImpl::lock_shared(boost::system::error_code& ec)
{
  std::unique_lock lock{mutex};

  if (exclusive_queue.empty() && state < MaxShared) {
    state++;
    ec.clear();
  } else {
    SyncRequest request;
    shared_queue.push_back(request);
    request.cond.wait(lock, [&request] { return request.ec; });
    ec = *request.ec;
  }
}

inline void SharedMutexImpl::lock_shared()
{
  boost::system::error_code ec;
  lock_shared(ec);
  if (ec) {
    throw boost::system::system_error(ec);
  }
}

void SharedMutexImpl::unlock_shared()
{
  std::lock_guard lock{mutex};
  assert(state != Unlocked && state <= MaxShared);

  if (state == 1 && !exclusive_queue.empty()) {
    // grant next exclusive lock
    state = Exclusive;
    auto& request = exclusive_queue.front();
    exclusive_queue.pop_front();
    complete(&request, boost::system::error_code{});
  } else if (state == MaxShared && !shared_queue.empty() &&
             exclusive_queue.empty()) {
    // grant next shared lock
    auto& request = shared_queue.front();
    shared_queue.pop_front();
    complete(&request, boost::system::error_code{});
  } else {
    state--;
  }
}

void SharedMutexImpl::cancel()
{
  RequestList canceled;
  {
    std::lock_guard lock{mutex};
    canceled.splice(canceled.end(), shared_queue);
    canceled.splice(canceled.end(), exclusive_queue);
  }
  complete(std::move(canceled), boost::asio::error::operation_aborted);
}

inline void SharedMutexImpl::complete(LockRequest *request,
                                      boost::system::error_code ec)
{
  if (request->type == Type::Sync) {
    auto r = static_cast<SyncRequest*>(request);
    r->ec = ec;
    r->cond.notify_one();
  } else {
    auto r = static_cast<AsyncRequest*>(request);
    post(std::unique_ptr<AsyncRequest>{r}, ec);
  }
}

void SharedMutexImpl::complete(RequestList&& requests,
                               boost::system::error_code ec)
{
  try {
    while (!requests.empty()) {
      auto& request = requests.front();
      requests.pop_front();
      complete(&request, ec);
    }
  } catch (...) {
    // if the callback throws, clean up any remaining completions
    requests.clear_and_dispose([] (LockRequest *request) {
        if (request->type == Type::Async) {
          delete static_cast<AsyncRequest*>(request);
        } else {
          // nothing, SyncRequests are on the stack
        }
      });
    throw;
  }
}

} // namespace ceph::async::detail

#endif // CEPH_ASYNC_DETAIL_SHARED_MUTEX_H
