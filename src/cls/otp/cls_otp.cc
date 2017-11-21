// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/** \file
 *
 * This is an OSD class that implements methods for management
 * and use of otp (one time password).
 *
 */

#include <errno.h>
#include <map>
#include <list>

#include <boost/range/adaptor/reversed.hpp>

#include "include/types.h"
#include "include/utime.h"
#include "objclass/objclass.h"

#include "common/errno.h"
#include "common/Clock.h"

#include "cls/otp/cls_otp_ops.h"
#include "cls/otp/cls_otp_types.h"


using namespace rados::cls::otp;


CLS_VER(1,0)
CLS_NAME(otp)

#define ATTEMPTS_PER_WINDOW 5

static string otp_key_prefix = "otp.";

struct otp_header {
  set<string> ids;

  otp_header() {}

  void encode(bufferlist &bl) const {
    ENCODE_START(1, 1, bl);
    ::encode(ids, bl);
    ENCODE_FINISH(bl);
  }
  void decode(bufferlist::iterator &bl) {
    DECODE_START(1, bl);
    ::decode(ids, bl);
    DECODE_FINISH(bl);
  }
};
WRITE_CLASS_ENCODER(otp_header)

struct otp_instance {
  otp_info_t otp;

  list<otp_check_t> last_checks; 
  uint32_t last_success{0}; /* otp counter/step of last successful check */

  otp_instance() {}

  void encode(bufferlist &bl) const {
    ENCODE_START(1, 1, bl);
    ::encode(otp, bl);
    ::encode(last_checks, bl);
    ::encode(last_success, bl);
    ENCODE_FINISH(bl);
  }
  void decode(bufferlist::iterator &bl) {
    DECODE_START(1, bl);
    ::decode(otp, bl);
    ::decode(last_checks, bl);
    ::decode(last_success, bl);
    DECODE_FINISH(bl);
  }

  void trim_expired(const ceph::real_time& now);
  void check(const string& token, const string& val, bool *update);
  bool verify(const ceph::real_time& timestamp, const string& val);

  void find(const string& token, otp_check_t *result);
};
WRITE_CLASS_ENCODER(otp_instance)


void otp_instance::trim_expired(const ceph::real_time& now)
{
  ceph::real_time window_start = now - make_timespan(otp.step_size);

  while (!last_checks.empty() &&
         last_checks.front().timestamp < window_start) {
    last_checks.pop_front();
  }
}

void otp_instance::check(const string& token, const string& val, bool *update)
{
  ceph::real_time now = ceph::real_clock::now();
  trim_expired(now);

  if (last_checks.size() >= ATTEMPTS_PER_WINDOW) {
    /* too many attempts */
    *update = false;
    return;
  }

  otp_check_t check;
  check.token = token;
  check.timestamp = now;
  check.result = (verify(now, val) ? OTP_CHECK_SUCCESS : OTP_CHECK_FAIL);

  last_checks.push_back(check);

  *update = true;
}

bool otp_instance::verify(const ceph::real_time& timestamp, const string& val)
{
#warning FIXME should be based on result from library
  uint32_t secs = (uint32_t)ceph::real_clock::to_time_t(timestamp);
  int step_size = (otp.step_size ? otp.step_size : 1);
  int step_window = otp.window * step_size;

  if (step_window > (uint32_t)secs / 2) {
    step_window = secs / 2;
  }

  for (int s = -step_window / 2; s <= step_window / 2; s += step_size) {
    uint32_t index = (secs + s) / step_size;
    if (index <= last_success) { /* already used value */
      continue;
    }

#warning FIXME check here is temporary
    char buf[otp.seed.size() + 16];
    snprintf(buf, sizeof(buf), "%s:%d", otp.seed.c_str(), (int)index);
    if (val == buf) {
      last_success = index;
      return true;
    }
  }

  return false;
}

void otp_instance::find(const string& token, otp_check_t *result)
{
  ceph::real_time now = real_clock::now();
  trim_expired(now);

  for (auto entry : boost::adaptors::reverse(last_checks)) {
    if (entry.token == token) {
      *result = entry;
      return;
    }
  }
  result->token = token;
  result->result = OTP_CHECK_UNKNOWN;
  result->timestamp = now;
}

static int get_otp_instance(cls_method_context_t hctx, const string& id, otp_instance *instance)
{
  bufferlist bl;
  string key = otp_key_prefix + id;
 
  int r = cls_cxx_map_get_val(hctx, key.c_str(), &bl);
  if (r < 0) {
    if (r != -ENOENT) {
      CLS_ERR("error reading key %s: %d", key.c_str(), r);
    }
    return r;
  }

  try {
    bufferlist::iterator it = bl.begin();
    ::decode(*instance, it);
  } catch (const buffer::error &err) {
    CLS_ERR("ERROR: failed to decode %s", key.c_str());
    return -EIO;
  }

  return 0;
}

static int write_otp_instance(cls_method_context_t hctx, const otp_instance& instance)
{
  string key = otp_key_prefix + instance.otp.id;

  bufferlist bl;
  ::encode(instance, bl);

  int r = cls_cxx_map_set_val(hctx, key.c_str(), &bl);
  if (r < 0) {
    CLS_ERR("ERROR: %s(): failed to store key (otp id=%s, r=%d)", __func__, instance.otp.id.c_str(), r);
    return r;
  }

  return 0;
}

static int remove_otp_instance(cls_method_context_t hctx, const string& id)
{
  string key = otp_key_prefix + id;

  int r = cls_cxx_map_remove_key(hctx, key.c_str());
  if (r < 0) {
    CLS_ERR("ERROR: %s(): failed to remove key (otp id=%s, r=%d)", __func__, id.c_str(), r);
    return r;
  }

  return 0;
}

static int read_header(cls_method_context_t hctx, otp_header *h)
{
  bufferlist bl;
  ::encode(h, bl);
  int r = cls_cxx_map_read_header(hctx, &bl);
  if (r == -ENOENT || r == -ENODATA) {
    *h = otp_header();
    return 0;
  }
  if (r < 0) {
    CLS_ERR("ERROR: %s(): failed to read map header (r=%d)", __func__, r);
    return r;
  }

  if (bl.length() == 0) {
    *h = otp_header();
    return 0;
  }

  auto iter = bl.begin();
  try {
    ::decode(*h, iter);
  } catch (buffer::error& err) {
    CLS_ERR("failed to decode otp_header");
    return -EIO;
  }

  return 0;
}

static int write_header(cls_method_context_t hctx, const otp_header& h)
{
  bufferlist bl;
  ::encode(h, bl);

  int r = cls_cxx_map_write_header(hctx, &bl);
  if (r < 0) {
    CLS_ERR("failed to store map header (r=%d)", r);
    return r;
  }

  return 0;
}

static int otp_set_op(cls_method_context_t hctx,
                       bufferlist *in, bufferlist *out)
{
  CLS_LOG(20, "%s", __func__);
  cls_otp_set_otp_op op;
  try {
    auto iter = in->begin();
    ::decode(op, iter);
  } catch (const buffer::error &err) {
    CLS_ERR("ERROR: %s(): failed to decode request", __func__);
    return -EINVAL;
  }

  otp_header h;
  bool update_header = false;
  int r = read_header(hctx, &h);
  if (r < 0) {
    return r;
  }

  for (auto entry : op.entries) {
    bool existed = (h.ids.find(entry.id) != h.ids.end());
    update_header = (update_header || !existed);

    otp_instance instance;
    instance.otp = entry;

    r = write_otp_instance(hctx, instance);
    if (r < 0) {
      return r;
    }

    h.ids.insert(entry.id);
  }

  if (update_header) {
    r = write_header(hctx, h);
    if (r < 0) {
      return r;
    }
  }

  return 0;
}

static int otp_remove_op(cls_method_context_t hctx,
                          bufferlist *in, bufferlist *out)
{
  CLS_LOG(20, "%s", __func__);
  cls_otp_remove_otp_op op;
  try {
    auto iter = in->begin();
    ::decode(op, iter);
  } catch (const buffer::error &err) {
    CLS_ERR("ERROR: %s(): failed to decode request", __func__);
    return -EINVAL;
  }

  otp_header h;
  bool removed_existing = false;
  int r = read_header(hctx, &h);
  if (r < 0) {
    return r;
  }

  for (auto id : op.ids) {
    bool existed = (h.ids.find(id) != h.ids.end());
    removed_existing = (removed_existing || existed);

    if (!existed) {
      continue;
    }

    r = remove_otp_instance(hctx, id);
    if (r < 0) {
      return r;
    }

    h.ids.erase(id);
  }

  if (removed_existing) {
    r = write_header(hctx, h);
    if (r < 0) {
      return r;
    }
  }

  return 0;
}

static int otp_get_op(cls_method_context_t hctx,
                          bufferlist *in, bufferlist *out)
{
  CLS_LOG(20, "%s", __func__);
  cls_otp_get_otp_op op;
  try {
    auto iter = in->begin();
    ::decode(op, iter);
  } catch (const buffer::error &err) {
    CLS_ERR("ERROR: %s(): failed to decode request", __func__);
    return -EINVAL;
  }

  cls_otp_get_otp_reply result;

  otp_header h;
  int r;

  r = read_header(hctx, &h);
  if (r < 0) {
    return r;
  }

  if (op.get_all) {
    op.ids.clear();
    for (auto id : h.ids) {
      op.ids.push_back(id);
    }
  }

  for (auto id : op.ids) {
    bool exists = (h.ids.find(id) != h.ids.end());

    if (!exists) {
      continue;
    }

    otp_instance instance;
    r = get_otp_instance(hctx, id, &instance);
    if (r < 0) {
      return r;
    }

    result.found_entries.push_back(instance.otp);
  }

  ::encode(result, *out);

  return 0;
}

static int otp_check_op(cls_method_context_t hctx,
                          bufferlist *in, bufferlist *out)
{
  CLS_LOG(20, "%s", __func__);
  cls_otp_check_otp_op op;
  try {
    auto iter = in->begin();
    ::decode(op, iter);
  } catch (const buffer::error &err) {
    CLS_ERR("ERROR: %s(): failed to decode request", __func__);
    return -EINVAL;
  }

  otp_header h;
  int r;

  otp_instance instance;

  r = get_otp_instance(hctx, op.id, &instance);
  if (r < 0) {
    return r;
  }

  bool update{false};
  instance.check(op.token, op.val, &update);

  if (update) {
    r = write_otp_instance(hctx, instance);
    if (r < 0) {
      return r;
    }
  }

  return 0;
}

static int otp_get_result(cls_method_context_t hctx,
                          bufferlist *in, bufferlist *out)
{
  CLS_LOG(20, "%s", __func__);
  cls_otp_check_otp_op op;
  try {
    auto iter = in->begin();
    ::decode(op, iter);
  } catch (const buffer::error &err) {
    CLS_ERR("ERROR: %s(): failed to decode request", __func__);
    return -EINVAL;
  }

  otp_header h;
  int r;

  otp_instance instance;

  r = get_otp_instance(hctx, op.id, &instance);
  if (r < 0) {
    return r;
  }

  cls_otp_get_result_reply reply;
  instance.find(op.token, &reply.result);
  ::encode(reply, *out);

  return 0;
}

CLS_INIT(otp)
{
  CLS_LOG(20, "Loaded otp class!");

  cls_handle_t h_class;
  cls_method_handle_t h_set_otp_op;
  cls_method_handle_t h_get_otp_op;
  cls_method_handle_t h_check_otp_op;
  cls_method_handle_t h_get_result_op; /*
                                        * need to check and get check result in two phases. The
                                        * reason is that we need to update failure internally,
                                        * however, there's no way to both return a failure and
                                        * update, because a failure will cancel the operation,
                                        * and write operations will not return a value. So
                                        * we're returning a success, potentially updating the
                                        * status internally, then a subsequent request can try
                                        * to fetch the status. If it fails it means that failed
                                        * to authenticate.
                                        */
  cls_method_handle_t h_remove_otp_op;

  cls_register("otp", &h_class);
  cls_register_cxx_method(h_class, "otp_set",
                          CLS_METHOD_RD | CLS_METHOD_WR,
                          otp_set_op, &h_set_otp_op);
  cls_register_cxx_method(h_class, "otp_get",
                          CLS_METHOD_RD,
                          otp_get_op, &h_get_otp_op);
  cls_register_cxx_method(h_class, "otp_check",
                          CLS_METHOD_RD | CLS_METHOD_WR,
                          otp_check_op, &h_check_otp_op);
  cls_register_cxx_method(h_class, "otp_get_result",
                          CLS_METHOD_RD,
                          otp_get_result, &h_get_result_op);
  cls_register_cxx_method(h_class, "otp_remove",
                          CLS_METHOD_RD | CLS_METHOD_WR,
                          otp_remove_op, &h_remove_otp_op);

  return;
}