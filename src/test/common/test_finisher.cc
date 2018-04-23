// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2018 Red Hat <contact@redhat.com>
 *
 * LGPL2.1 (see COPYING-LGPL2.1) or later
 */

#include <atomic>

#include <gtest/gtest.h>

#include "include/Context.h"
#include "common/Cond.h"
#include "common/Finisher.h"

TEST(UpDown, Finisher) {
  auto cct = (new CephContext(CEPH_ENTITY_TYPE_CLIENT))->get();
  Finisher f(cct);
  f.start();
  f.stop();
  cct->put();
}

TEST(Fire, Finisher) {
  auto cct = (new CephContext(CEPH_ENTITY_TYPE_CLIENT))->get();
  Finisher f(cct);
  C_SaferCond c;
  f.start();
  f.queue(&c);
  c.wait();
  f.stop();
  cct->put();
}

atomic<bool> fired = false;

TEST(FireFuction, Finisher) {
  auto cct = (new CephContext(CEPH_ENTITY_TYPE_CLIENT))->get();
  Finisher f(cct);
  f.start();
  ASSERT_FALSE(fired);
  f.queue([]() { fired = true; });
  std::this_thread::sleep_for(5s);
  ASSERT_TRUE(fired);
  f.stop();
  cct->put();
}
