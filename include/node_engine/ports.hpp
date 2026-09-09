#pragma once

#include "node_engine/pin.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace node_engine {

struct PortDecl {
  std::string id;
  TypeId type = TypeId::Dynamic;
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
    decls_.push_back(PortDecl{
        std::move(id),
        type_id_for<T>(),
        true,
        {},
        false,
    });
    return PortBuilder{*this, decls_.size() - 1};
  }

  template <typename T>
  PortBuilder out(std::string id) {
    decls_.push_back(PortDecl{
        std::move(id),
        type_id_for<T>(),
        false,
        {},
        false,
    });
    return PortBuilder{*this, decls_.size() - 1};
  }

  // FloatBuffer sugar
  PortBuilder in_buffer(std::string id) {
    decls_.push_back(PortDecl{std::move(id), TypeId::FloatBuffer, true, {}, false});
    return PortBuilder{*this, decls_.size() - 1};
  }

  PortBuilder out_buffer(std::string id) {
    decls_.push_back(PortDecl{std::move(id), TypeId::FloatBuffer, false, {}, false});
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
  if constexpr (std::is_same_v<T, float>) {
    d.literal = Value{static_cast<double>(value)};
  } else {
    d.literal = Value{std::move(value)};
  }
  d.has_literal = true;
  return *this;
}

}  // namespace node_engine
