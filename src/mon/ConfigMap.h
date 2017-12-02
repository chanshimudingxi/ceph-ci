// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include <map>
#include <ostream>
#include <string>

#include "common/options.h"
#include "common/entity_name.h"

class CrushWrapper;

// the precedence is thus:
//
//  global
//   crush location (coarse to fine, ordered by type id)
//  daemon type (e.g., osd)
//   device class (osd only)
//   crush location (coarse to fine, ordered by type id)
//  daemon name (e.g., mds.foo)
//
// Note that this means that if we have
//
//  config/host:foo/a = 1
//  config/osd/rack:foo/a = 2
//
// then we get a = 2.  The osd-level config wins, even though rack
// is less precise than host, because the crush limiters are only
// resolved within a section (global, per-daemon, per-instance).

struct MaskedOption {
  string raw_value;                          ///< raw, unparsed, unvalidated value
  Option opt;                                ///< the option
  std::string location_type, location_value; ///< matches crush_location
  std::string device_class;                  ///< matches device class

  MaskedOption(const Option& o) : opt(o) {}

  /// return a precision metric (smaller is more precise)
  int get_precision(const CrushWrapper *crush);

  friend ostream& operator<<(ostream& out, const MaskedOption& o);

  void dump(Formatter *f) const;
};

struct Section {
  std::multimap<std::string,MaskedOption> options;

  void clear() {
    options.clear();
  }
  void dump(Formatter *f) const;
};

struct ConfigMap {
  Section global;
  std::map<std::string,Section> by_type;
  std::map<std::string,Section> by_id;

  void clear() {
    global.clear();
    by_type.clear();
    by_id.clear();
  }
  void dump(Formatter *f) const;
  void generate_entity_map(
    const EntityName& name,
    const map<std::string,std::string>& crush_location,
    const CrushWrapper *crush,
    const std::string& device_class,
    std::map<std::string,std::string> *out);
};
