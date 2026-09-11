#pragma once

#include "node_engine/pin.hpp"
#include "node_engine/type_registry.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace node_engine {

struct PortDecl {
  std::string id;
  TypeId type = TypeId::Dynamic;
  std::string type_key;
  bool is_input = true;
  Value literal{};
  bool has_literal = false;
};

class Ports;

class PortBuilder {
 public:
  PortBuilder(Ports& ports, std::size_t index) : ports_(ports), index_(index) {}

  template <typename T>
  PortBuilder& literal(T value);

 private:
  Ports& ports_;
  std::size_t index_;
};

class Ports {
 public:
  template <typename T>
  PortBuilder in(std::string id) {
    PortDecl d;
    d.id = std::move(id);
    d.type = type_id_for<T>();
    d.is_input = true;
    if constexpr (!is_builtin_pin_v<T>) {
      d.type_key = type_key_for<T>();
    }
    decls_.push_back(std::move(d));
    return PortBuilder{*this, decls_.size() - 1};
  }

  template <typename T>
  PortBuilder out(std::string id) {
    PortDecl d;
    d.id = std::move(id);
    d.type = type_id_for<T>();
    d.is_input = false;
    if constexpr (!is_builtin_pin_v<T>) {
      d.type_key = type_key_for<T>();
    }
    decls_.push_back(std::move(d));
    return PortBuilder{*this, decls_.size() - 1};
  }

  PortBuilder in_buffer(std::string id) {
    decls_.push_back(PortDecl{std::move(id), TypeId::FloatBuffer, {}, true, {}, false});
    return PortBuilder{*this, decls_.size() - 1};
  }

  PortBuilder out_buffer(std::string id) {
    decls_.push_back(PortDecl{std::move(id), TypeId::FloatBuffer, {}, false, {}, false});
    return PortBuilder{*this, decls_.size() - 1};
  }

  PortBuilder in_buffer_f32(std::string id) {
    decls_.push_back(PortDecl{std::move(id), TypeId::Float32Buffer, {}, true, {}, false});
    return PortBuilder{*this, decls_.size() - 1};
  }

  PortBuilder out_buffer_f32(std::string id) {
    decls_.push_back(PortDecl{std::move(id), TypeId::Float32Buffer, {}, false, {}, false});
    return PortBuilder{*this, decls_.size() - 1};
  }

  PortBuilder in_bytes(std::string id) {
    decls_.push_back(PortDecl{std::move(id), TypeId::Uint8Buffer, {}, true, {}, false});
    return PortBuilder{*this, decls_.size() - 1};
  }

  PortBuilder out_bytes(std::string id) {
    decls_.push_back(PortDecl{std::move(id), TypeId::Uint8Buffer, {}, false, {}, false});
    return PortBuilder{*this, decls_.size() - 1};
  }

  std::vector<PortDecl> const& decls() const { return decls_; }
  std::vector<PortDecl>& decls() { return decls_; }

 private:
  friend class PortBuilder;
  std::vector<PortDecl> decls_;
};

template <typename T>
PortBuilder& PortBuilder::literal(T value) {
  auto& d = ports_.decls_.at(index_);
  if constexpr (is_builtin_pin_v<T>) {
    d.literal = Value{std::move(value)};
  } else {
    d.literal = Value{TypeRegistry::instance().make_object_value(std::move(value))};
  }
  d.has_literal = true;
  return *this;
}

}  // namespace node_engine
