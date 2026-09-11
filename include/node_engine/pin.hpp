#pragma once

#include "node_engine/converter_registry.hpp"
#include "node_engine/value.hpp"

#include <string>

namespace node_engine {

struct Pin {
  std::string id;
  TypeId type = TypeId::Dynamic;
  // For TypeId::Object: registered type key (e.g. "demo.SensorSample").
  std::string type_key;
  Value literal{};
  bool connected = false;
  Value buffer{};
};

inline Value const& read_pin(Pin const& pin) {
  return pin.connected ? pin.buffer : pin.literal;
}

inline Value read_pin_as(Pin const& pin, TypeId want, std::string const& want_key = {}) {
  Value const& raw = read_pin(pin);
  TypeId have = type_of(raw);
  std::string from_key = value_type_key(raw);
  std::string to_key = want_key.empty() ? pin_type_key(want, {}) : want_key;

  if (want == TypeId::Dynamic || have == TypeId::Dynamic) {
    return clone_value(raw);
  }
  if (from_key == to_key) {
    return clone_value(raw);
  }
  if (types_compatible(from_key, to_key, have, want)) {
    return apply_converter(raw, from_key, to_key);
  }
  return clone_value(raw);
}

}  // namespace node_engine
