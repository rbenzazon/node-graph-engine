#pragma once

#include "node_engine/pin.hpp"
#include "node_engine/ports.hpp"
#include "node_engine/type_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace node_engine {

enum class Role {
  Producer,
  Transformer,
  Consumer,
  BoundaryIn,
  BoundaryOut,
  GraphWrapper,
};

enum class ParallelMode {
  None,
  OnCollection,
  Always,
};

struct ParallelHint {
  ParallelMode mode = ParallelMode::None;
  std::size_t grain_size = 0;  // 0 = engine / Taskflow default chunking
};

struct Slice {
  std::size_t begin = 0;
  std::size_t end = 1;  // [begin, end)
};

struct Meta {
  std::string_view type_id;
  Role role = Role::Transformer;
};

inline bool rising_edge(bool prev, bool cur) {
  return !prev && cur;
}

class Node {
 public:
  virtual ~Node() = default;

  virtual std::string_view type_id() const = 0;
  virtual Role role() const { return Role::Transformer; }

  // Single entry point — engine always calls this.
  virtual void compute(Slice slice) = 0;

  virtual ParallelHint parallel_hint() const { return {}; }
  virtual std::size_t parallel_length() const { return 0; }
  virtual void propagate_types() {}

  // Deep copy for GraphNode instance expansion / flatten.
  virtual std::unique_ptr<Node> clone() const = 0;

  void emit() { dirty = true; }
  void suppress() { dirty = false; }

  Pin* find_input(std::string_view id);
  Pin const* find_input(std::string_view id) const;
  Pin* find_output(std::string_view id);
  Pin const* find_output(std::string_view id) const;

  template <typename T>
  T get(std::string_view pin_id) const {
    Pin const* p = find_input(pin_id);
    if (!p) {
      throw std::runtime_error(std::string("missing input pin: ") + std::string(pin_id));
    }
    std::string want_key;
    if constexpr (is_builtin_pin_v<T>) {
      want_key = builtin_type_key(type_id_for<T>());
    } else {
      want_key = type_key_for<T>();
    }
    Value v = read_pin_as(*p, type_id_for<T>(), want_key);
    if constexpr (is_builtin_pin_v<T>) {
      return std::get<T>(v);
    } else {
      return TypeRegistry::instance().cast<T>(std::get<ObjectValue>(v));
    }
  }

  template <typename T>
  void set_out(std::string_view pin_id, T value) {
    Pin* p = find_output(pin_id);
    if (!p) {
      throw std::runtime_error(std::string("missing output pin: ") + std::string(pin_id));
    }
    if constexpr (is_builtin_pin_v<T>) {
      p->buffer = Value{std::move(value)};
    } else {
      p->buffer = Value{TypeRegistry::instance().make_object_value(std::move(value))};
      if (p->type_key.empty()) {
        p->type_key = type_key_for<T>();
      }
    }
    p->connected = true;
  }

  std::vector<double> const& buffer_in(std::string_view pin_id) const {
    Pin const* p = find_input(pin_id);
    if (!p) {
      throw std::runtime_error(std::string("missing input pin: ") + std::string(pin_id));
    }
    Value const& v = read_pin(*p);
    return std::get<std::vector<double>>(v);
  }

  std::vector<double>& buffer_out(std::string_view pin_id) {
    Pin* p = find_output(pin_id);
    if (!p) {
      throw std::runtime_error(std::string("missing output pin: ") + std::string(pin_id));
    }
    if (!std::holds_alternative<std::vector<double>>(p->buffer)) {
      p->buffer = std::vector<double>{};
    }
    p->connected = true;
    return std::get<std::vector<double>>(p->buffer);
  }

  void apply_ports(Ports const& ports);

  std::vector<Pin> inputs;
  std::vector<Pin> outputs;
  bool dirty = true;
};

// CRTP helper: ports()/meta/parallel + clone via copy ctor.
template <typename Derived>
class NodeBase : public Node {
 public:
  NodeBase() {
    Ports p;
    Derived::ports(p);
    apply_ports(p);
  }

  std::string_view type_id() const override { return Derived::meta.type_id; }
  Role role() const override { return Derived::meta.role; }

  ParallelHint parallel_hint() const override {
    if constexpr (requires { Derived::parallel; }) {
      return Derived::parallel;
    } else {
      return {};
    }
  }

  std::unique_ptr<Node> clone() const override {
    return std::make_unique<Derived>(static_cast<Derived const&>(*this));
  }
};

}  // namespace node_engine
