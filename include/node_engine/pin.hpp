#pragma once

#include "node_engine/value.hpp"

#include <string>

namespace node_engine {

struct Pin {
  std::string id;
  TypeId type = TypeId::Dynamic;
  Value literal{};
  bool connected = false;
  Value buffer{};
};

inline Value const& read_pin(Pin const& pin) {
  return pin.connected ? pin.buffer : pin.literal;
}

inline Value read_pin_as(Pin const& pin, TypeId want) {
  Value const& raw = read_pin(pin);
  TypeId have = type_of(raw);
  if (want == TypeId::Dynamic || have == want || have == TypeId::Dynamic) {
    return raw;
  }
  if (can_autoconvert(have, want)) {
    return autoconvert(raw, want);
  }
  return raw;
}

}  // namespace node_engine
