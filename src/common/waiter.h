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

#ifndef CEPH_COMMON_WAITER_H
#define CEPH_COMMON_WAITER_H

#include <condition_variable>
#include <mutex>
#include <tuple>

namespace ceph {
namespace waiter_detail {
class waiter_base {
protected:
  std::mutex lock;
  using unique_lock = std::unique_lock<decltype(lock)>;
  std::condition_variable cond;
  bool done = false;

  ~waiter_base() = default;

  unique_lock wait_base() {
    unique_lock l(lock);
    cond.wait(l, [this](){ return done; });
    done = false; // If someone waits, we can be called again
    return l;
  }

  unique_lock exec_base() {
    unique_lock l(lock);
    // There's no really good way to handle being called twice
    // without being reset.
    assert(!done);
    done = true;
    cond.notify_one();
    return l;
  }
};

// waiter is a replacement for C_SafeCond and friends. It is the
// moral equivalent of a future but plays well with a world of
// callbacks.

template<typename ...S>
class waiter;

template<>
class waiter<void> : public waiter_base {
public:
  void wait() {
    wait_base();
  }

  void operator()() {
    exec_base();
  }
};

template<typename Ret>
class waiter<Ret> : public waiter_base {
  Ret ret;

  struct _w {
    waiter* t;
    _w(waiter* t) : t(t) {}
    _w(_w&& o) : t(o.t) {}
    _w& operator =(_w&& o) {
      t = o.t;
      return *this;
    }
    void operator()(Ret&& r) {
      (*t)(std::move(r));
    }

    void operator()(const Ret& r) {
      (*t)(r);
    }
  };
public:
  Ret&& wait() {
    unique_lock l(wait_base());
    return std::move(ret);
  }

  void operator()(Ret&& _ret) {
    unique_lock l(exec_base());
    ret = std::move(_ret);
  }

  void operator()(const Ret& _ret) {
    unique_lock l(exec_base());
    ret = _ret;
  }
};

template<typename ...Ret>
class waiter : public waiter_base {
  std::tuple<Ret...> ret;

  struct _w {
    waiter<Ret...>* t;
    _w(waiter<Ret...>* t) : t(t) {}
    _w(_w&& o) : t(o.t) {}
    _w& operator =(_w&& o) {
      t = o.t;
      return *this;
    }
    void operator()(Ret&&... r) {
      (*t)(std::move(r)...);
    }

    void operator()(const Ret&... r) {
      (*t)(r...);
    }
  };
public:
  std::tuple<Ret...>&& wait() {
    unique_lock l(wait_base());
    return std::move(ret);
  }

  void operator()(Ret&&... _ret) {
    unique_lock l(exec_base());
    ret = std::forward_as_tuple(_ret...);
  }

  void operator()(const Ret&... _ret) {
    unique_lock l(exec_base());
    ret = std::forward_as_tuple(_ret...);
  }
};
}
using waiter_detail::waiter;


// For safety reasons (avoiding undefined behavior around sequence
// points) std::reference_wrapper disallows move construction. This
// harms us in cases where we want to pass a reference in to something
// that unavoidably moves.
//
// It should not be used generally.
template<typename T>
class rvalue_reference_wrapper {
public:
  // types
  using type = T;

  rvalue_reference_wrapper(T& r) noexcept
    : p(std::addressof(r)) {}

  // We write our semantics to match those of reference collapsing. If
  // we're treated as an lvalue, collapse to one.

  rvalue_reference_wrapper(const rvalue_reference_wrapper&) noexcept = default;
  rvalue_reference_wrapper(rvalue_reference_wrapper&&) noexcept = default;

  // assignment
  rvalue_reference_wrapper& operator=(
    const rvalue_reference_wrapper& x) noexcept = default;
  rvalue_reference_wrapper& operator=(
    rvalue_reference_wrapper&& x) noexcept = default;

  operator T& () const noexcept {
    return *p;
  }
  T& get() const noexcept {
    return *p;
  }

  operator T&& () noexcept {
    return std::move(*p);
  }
  T&& get() noexcept {
    return std::move(*p);
  }

  template<typename... Args>
  std::result_of_t<T&(Args&&...)> operator ()(Args&&... args ) const {
    return (*p)(std::forward<Args>(args)...);
  }

  template<typename... Args>
  std::result_of_t<T&&(Args&&...)> operator ()(Args&&... args ) {
    return std::move(*p)(std::forward<Args>(args)...);
  }

private:
  T* p;
};

template<typename T>
rvalue_reference_wrapper<T> rref(T& t) {
  return rvalue_reference_wrapper<T>(t);
}
}

#endif // CEPH_COMMON_WAITER_H
