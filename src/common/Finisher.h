// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_FINISHER_H
#define CEPH_FINISHER_H

#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "include/Context.h"

#include "common/convenience.h"
#include "common/perf_counters.h"

class CephContext;

/// Finisher queue length performance counter ID.
enum {
  l_finisher_first = 997082,
  l_finisher_queue_len,
  l_finisher_complete_lat,
  l_finisher_last
};

/** @brief Asynchronous cleanup class.
 * Finisher asynchronously completes Contexts, which are simple classes
 * representing callbacks, in a dedicated worker thread. Enqueuing
 * contexts to complete is thread-safe.
 */
class Finisher {
  CephContext *cct;
  std::mutex finisher_lock; ///< Protects access to queues and
			    ///  finisher_running.
  std::condition_variable finisher_cond; ///< Signaled when there is
					 ///< something to process.
  std::condition_variable finisher_empty_cond; ///< Signaled when the
					       ///< finisher has nothing
					       ///< more to process.
  bool finisher_stop = false; ///< Set when the finisher should stop.
  bool finisher_running = false; ///< True when the finisher is currently
                                 ///  executing contexts.
  bool finisher_empty_wait = false; ///< True mean someone wait finisher empty.
  const std::string thread_name{"fn_anonymous"};

  /// Queue for contexts for which complete(0) will be called.
  std::vector<std::pair<Context*, int>> finisher_queue;

  /// Performance counter for the finisher's queue length.
  /// Only active for named finishers.
  PerfCountersRef logger;

  std::thread finisher_thread;

 public:
  /// Add a context to complete, optionally specifying a parameter for
  /// the complete function.
  void queue(Context *c, int r = 0) {
    std::lock_guard l(finisher_lock);
    if (finisher_queue.empty()) {
      finisher_cond.notify_all();
    }
    finisher_queue.push_back(make_pair(c, r));
    if (logger)
      logger->inc(l_finisher_queue_len);
  }

  void queue(std::list<Context*>& ls) {
    {
      std::lock_guard l(finisher_lock);
      if (finisher_queue.empty()) {
	finisher_cond.notify_all();
      }
      for (auto i : ls) {
	finisher_queue.push_back(make_pair(i, 0));
      }
      if (logger)
	logger->inc(l_finisher_queue_len, ls.size());
    }
    ls.clear();
  }
  void queue(std::deque<Context*>& ls) {
    {
      std::lock_guard l(finisher_lock);
      if (finisher_queue.empty()) {
	finisher_cond.notify_all();
      }
      for (auto i : ls) {
	finisher_queue.push_back(make_pair(i, 0));
      }
      if (logger)
	logger->inc(l_finisher_queue_len, ls.size());
    }
    ls.clear();
  }
  void queue(std::vector<Context*>& ls) {
    {
      std::lock_guard l(finisher_lock);
      if (finisher_queue.empty()) {
	finisher_cond.notify_all();
      }
      for (auto i : ls) {
	finisher_queue.push_back(make_pair(i, 0));
      }
      if (logger)
	logger->inc(l_finisher_queue_len, ls.size());
    }
    ls.clear();
  }

  /// Start the worker thread.
  void start();

  /** @brief Stop the worker thread.
   *
   * Does not wait until all outstanding contexts are completed.
   * To ensure that everything finishes, you should first shut down
   * all sources that can add contexts to this finisher and call
   * wait_for_empty() before calling stop(). */
  void stop();

  /** @brief Blocks until the finisher has nothing left to process.
   *
   * This function will also return when a concurrent call to stop()
   * finishes, but this class should never be used in this way. */
  void wait_for_empty();

  /// The worker function of the Finisher
  void finisher_thread_entry() noexcept;

  /// Construct an anonymous Finisher.
  /// Anonymous finishers do not log their queue length.
  explicit Finisher(CephContext *cct) : cct(cct) {}

  /// Construct a named Finisher that logs its queue length.
  Finisher(CephContext *cct, std::string name, std::string tn)
    : cct(cct), thread_name(std::move(tn)) {
    PerfCountersBuilder b(cct, std::string("finisher-") + name,
			  l_finisher_first, l_finisher_last);
    b.add_u64(l_finisher_queue_len, "queue_len");
    b.add_time_avg(l_finisher_complete_lat, "complete_latency");
    logger = { b.create_perf_counters(), cct };
    cct->get_perfcounters_collection()->add(logger.get());
    logger->set(l_finisher_queue_len, 0);
    logger->set(l_finisher_complete_lat, 0);
  }

  ~Finisher() = default;
};

/// Context that is completed asynchronously on the supplied finisher.
class C_OnFinisher : public Context {
  Context *con;
  Finisher *fin;
public:
  C_OnFinisher(Context *c, Finisher *f) : con(c), fin(f) {
    assert(fin != NULL);
    assert(con != NULL);
  }

  ~C_OnFinisher() override {
    if (con != nullptr) {
      delete con;
      con = nullptr;
    }
  }

  void finish(int r) override {
    fin->queue(con, r);
    con = nullptr;
  }
};

#endif
