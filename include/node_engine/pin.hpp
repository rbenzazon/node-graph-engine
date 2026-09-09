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

}  // namespace node_engine
