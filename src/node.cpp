#include "node_engine/node.hpp"

namespace node_engine {
namespace {

template <typename Pins>
Pin* find_pin(Pins& pins, std::string_view id) {
  for (auto& pin : pins) {
    if (pin.id == id) {
      return &pin;
    }
  }
  return nullptr;
}

template <typename Pins>
Pin const* find_pin_const(Pins const& pins, std::string_view id) {
  for (auto const& pin : pins) {
    if (pin.id == id) {
      return &pin;
    }
  }
  return nullptr;
}

}  // namespace

Pin* Node::find_input(std::string_view id) {
  return find_pin(inputs, id);
}

Pin const* Node::find_input(std::string_view id) const {
  return find_pin_const(inputs, id);
}

Pin* Node::find_output(std::string_view id) {
  return find_pin(outputs, id);
}

Pin const* Node::find_output(std::string_view id) const {
  return find_pin_const(outputs, id);
}

void Node::apply_ports(Ports const& ports) {
  inputs.clear();
  outputs.clear();
  for (auto const& decl : ports.decls()) {
    Pin pin;
    pin.id = decl.id;
    pin.type = decl.type;
    pin.type_key = decl.type_key;
    if (decl.has_literal) {
      pin.literal = decl.literal;
    }
    if (decl.is_input) {
      inputs.push_back(std::move(pin));
    } else {
      outputs.push_back(std::move(pin));
    }
  }
}

}  // namespace node_engine
