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

#include "common/async/shared_mutex.h"
#include <optional>
#include <shared_mutex>
#include <gtest/gtest.h>

// return a lambda that can be used as a callback to capture its error code
auto capture(std::optional<boost::system::error_code>& opt_ec)
{
  return [&] (boost::system::error_code ec) { opt_ec = ec; };
}

namespace ceph::async {

TEST(SharedMutex, async_exclusive)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::optional<boost::system::error_code> ec1, ec2, ec3;

  // request three exclusive locks
  mutex.async_lock(capture(ec1));
  mutex.async_lock(capture(ec2));
  mutex.async_lock(capture(ec3));

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);
  EXPECT_FALSE(ec3);

  context.poll();
  EXPECT_FALSE(context.stopped()); // second lock still pending

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);
  EXPECT_FALSE(ec2);

  mutex.unlock(); // release first lock

  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_FALSE(context.stopped());

  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::system::errc::success, *ec2);
  EXPECT_FALSE(ec3);

  mutex.unlock(); // release second lock

  EXPECT_FALSE(ec3);

  context.poll();
  EXPECT_TRUE(context.stopped());

  ASSERT_TRUE(ec3);
  EXPECT_EQ(boost::system::errc::success, *ec3);
}

TEST(SharedMutex, async_shared)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::optional<boost::system::error_code> ec1, ec2;

  // request two shared locks
  mutex.async_lock_shared(capture(ec1));
  mutex.async_lock_shared(capture(ec2));

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_TRUE(context.stopped());

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);
  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::system::errc::success, *ec2);
}

TEST(SharedMutex, async_lock_guard_adopt)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::optional<boost::system::error_code> ec1, ec2;

  mutex.async_lock(capture(ec1));
  mutex.async_lock(capture(ec2));

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_FALSE(context.stopped());

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);

  EXPECT_FALSE(ec2);

  {
    // matches enough of the BasicLockable concept for std::lock_guard to adopt
    std::lock_guard lock{mutex, std::adopt_lock};
  }

  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_TRUE(context.stopped());

  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::system::errc::success, *ec2);
}

TEST(SharedMutex, async_shared_lock_adopt)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::optional<boost::system::error_code> ec1, ec2;

  mutex.async_lock_shared(capture(ec1));
  mutex.async_lock(capture(ec2));

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_FALSE(context.stopped());

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);

  EXPECT_FALSE(ec2);

  {
    // matches enough of the SharedMutex concept for std::shared_lock to adopt
    std::shared_lock lock{mutex, std::adopt_lock};
  }

  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_TRUE(context.stopped());

  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::system::errc::success, *ec2);
}

TEST(SharedMutex, async_exclusive_while_shared)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::optional<boost::system::error_code> ec1, ec2;

  // request a shared and exclusive lock
  mutex.async_lock_shared(capture(ec1));
  mutex.async_lock(capture(ec2));

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_FALSE(context.stopped()); // second lock still pending

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);
  EXPECT_FALSE(ec2);

  mutex.unlock_shared(); // release first lock

  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_TRUE(context.stopped());

  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::system::errc::success, *ec2);
}

TEST(SharedMutex, async_shared_while_exclusive)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::optional<boost::system::error_code> ec1, ec2;

  // request an exclusive and shared lock
  mutex.async_lock(capture(ec1));
  mutex.async_lock_shared(capture(ec2));

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_FALSE(context.stopped()); // second lock still pending

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);
  EXPECT_FALSE(ec2);

  mutex.unlock(); // release first lock

  EXPECT_FALSE(ec2);

  context.poll();
  EXPECT_TRUE(context.stopped());

  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::system::errc::success, *ec2);
}

TEST(SharedMutex, async_prioritize_exclusive)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::optional<boost::system::error_code> ec1, ec2, ec3;

  // acquire a shared lock, then request an exclusive and another shared lock
  mutex.async_lock_shared(capture(ec1));
  mutex.async_lock(capture(ec2));
  mutex.async_lock_shared(capture(ec3));

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);
  EXPECT_FALSE(ec3);

  context.poll();
  EXPECT_FALSE(context.stopped());

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);
  EXPECT_FALSE(ec2);
  // exclusive waiter blocks the second shared lock
  EXPECT_FALSE(ec3);

  mutex.unlock_shared(); // release first lock

  EXPECT_FALSE(ec2);
  EXPECT_FALSE(ec3);

  context.poll();
  EXPECT_FALSE(context.stopped());

  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::system::errc::success, *ec2);
  EXPECT_FALSE(ec3);
}

TEST(SharedMutex, async_cancel)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::optional<boost::system::error_code> ec1, ec2, ec3, ec4;

  // request 2 exclusive and shared locks
  mutex.async_lock(capture(ec1));
  mutex.async_lock(capture(ec2));
  mutex.async_lock_shared(capture(ec3));
  mutex.async_lock_shared(capture(ec4));

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);
  EXPECT_FALSE(ec3);
  EXPECT_FALSE(ec4);

  context.poll();
  EXPECT_FALSE(context.stopped());

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);
  EXPECT_FALSE(ec2);
  EXPECT_FALSE(ec3);
  EXPECT_FALSE(ec4);

  mutex.cancel();

  EXPECT_FALSE(ec2);
  EXPECT_FALSE(ec3);
  EXPECT_FALSE(ec4);

  context.poll();
  EXPECT_TRUE(context.stopped());

  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::asio::error::operation_aborted, *ec2);
  ASSERT_TRUE(ec3);
  EXPECT_EQ(boost::asio::error::operation_aborted, *ec3);
  ASSERT_TRUE(ec4);
  EXPECT_EQ(boost::asio::error::operation_aborted, *ec4);
}

TEST(SharedMutex, async_destruct)
{
  boost::asio::io_context context;

  std::optional<boost::system::error_code> ec1, ec2, ec3, ec4;

  {
    SharedMutex mutex(context.get_executor());

    // request 2 exclusive and shared locks
    mutex.async_lock(capture(ec1));
    mutex.async_lock(capture(ec2));
    mutex.async_lock_shared(capture(ec3));
    mutex.async_lock_shared(capture(ec4));
  }

  EXPECT_FALSE(ec1); // no callbacks until poll()
  EXPECT_FALSE(ec2);
  EXPECT_FALSE(ec3);
  EXPECT_FALSE(ec4);

  context.poll();
  EXPECT_TRUE(context.stopped());

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);
  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::asio::error::operation_aborted, *ec2);
  ASSERT_TRUE(ec3);
  EXPECT_EQ(boost::asio::error::operation_aborted, *ec3);
  ASSERT_TRUE(ec4);
  EXPECT_EQ(boost::asio::error::operation_aborted, *ec4);
}

// return a capture() lambda that's bound to the given executor
template <typename Executor>
auto capture(const Executor& ex, std::optional<boost::system::error_code>& ec)
{
  return boost::asio::bind_executor(ex, ::capture(ec));
}

TEST(SharedMutex, cross_executor)
{
  boost::asio::io_context mutex_context;
  SharedMutex mutex(mutex_context.get_executor());

  boost::asio::io_context callback_context;
  auto ex2 = callback_context.get_executor();

  std::optional<boost::system::error_code> ec1, ec2;

  // request two exclusive locks
  mutex.async_lock(capture(ex2, ec1));
  mutex.async_lock(capture(ex2, ec2));

  EXPECT_FALSE(ec1);
  EXPECT_FALSE(ec2);

  mutex_context.poll();
  EXPECT_FALSE(mutex_context.stopped()); // maintains work on both executors

  EXPECT_FALSE(ec1); // no callbacks until poll() on callback_context
  EXPECT_FALSE(ec2);

  callback_context.poll();
  EXPECT_FALSE(callback_context.stopped()); // second lock still pending

  ASSERT_TRUE(ec1);
  EXPECT_EQ(boost::system::errc::success, *ec1);
  EXPECT_FALSE(ec2);

  mutex.unlock(); // release first lock

  mutex_context.poll();
  EXPECT_TRUE(mutex_context.stopped());

  EXPECT_FALSE(ec2);

  callback_context.poll();
  EXPECT_TRUE(callback_context.stopped());

  ASSERT_TRUE(ec2);
  EXPECT_EQ(boost::system::errc::success, *ec2);
}

TEST(SharedMutex, try_exclusive)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());
  {
    std::lock_guard lock{mutex};
    ASSERT_FALSE(mutex.try_lock()); // fail during exclusive
  }
  {
    std::shared_lock lock{mutex};
    ASSERT_FALSE(mutex.try_lock()); // fail during shared
  }
  ASSERT_TRUE(mutex.try_lock());
  mutex.unlock();
}

TEST(SharedMutex, try_shared)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());
  {
    std::lock_guard lock{mutex};
    ASSERT_FALSE(mutex.try_lock_shared()); // fail during exclusive
  }
  {
    std::shared_lock lock{mutex};
    ASSERT_TRUE(mutex.try_lock_shared()); // succeed during shared
    mutex.unlock_shared();
  }
  ASSERT_TRUE(mutex.try_lock_shared());
  mutex.unlock_shared();
}

TEST(SharedMutex, cancel)
{
  boost::asio::io_context context;
  SharedMutex mutex(context.get_executor());

  std::lock_guard l{mutex}; // exclusive lock blocks others

  // make synchronous lock calls in other threads
  auto f1 = std::async(std::launch::async, [&] { mutex.lock(); });
  auto f2 = std::async(std::launch::async, [&] { mutex.lock_shared(); });

  // this will race with spawned threads. just keep canceling until the
  // futures are ready
  const auto t = std::chrono::milliseconds(1);
  do { mutex.cancel(); } while (f1.wait_for(t) != std::future_status::ready);
  do { mutex.cancel(); } while (f2.wait_for(t) != std::future_status::ready);

  EXPECT_THROW(f1.get(), boost::system::system_error);
  EXPECT_THROW(f2.get(), boost::system::system_error);
}

} // namespace ceph::async
