#pragma once

#include "node_engine/pin.hpp"
#include "node_engine/ports.hpp"

#include <cstddef>
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
    Value v = read_pin_as(*p, type_id_for<T>());
    if constexpr (std::is_same_v<T, float>) {
      return static_cast<float>(std::get<double>(v));
    } else if constexpr (std::is_same_v<T, double>) {
      return std::get<double>(v);
    } else if constexpr (std::is_same_v<T, bool>) {
      return std::get<bool>(v);
    } else if constexpr (std::is_same_v<T, std::string>) {
      return std::get<std::string>(v);
    } else if constexpr (std::is_same_v<T, std::vector<double>>) {
      return std::get<std::vector<double>>(v);
    } else {
      static_assert(sizeof(T) == 0, "unsupported get<> type");
    }
  }

  template <typename T>
  void set_out(std::string_view pin_id, T value) {
    Pin* p = find_output(pin_id);
    if (!p) {
      throw std::runtime_error(std::string("missing output pin: ") + std::string(pin_id));
    }
    if constexpr (std::is_same_v<T, float>) {
      p->buffer = Value{static_cast<double>(value)};
    } else {
      p->buffer = Value{std::move(value)};
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
