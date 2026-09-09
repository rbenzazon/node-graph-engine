#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <memory>

namespace node_engine::nodes {

// Authoring-only. Flatten splices parent wires past these and drops them.
class BoundaryInFloat : public NodeBase<BoundaryInFloat> {
 public:
  static constexpr Meta meta{.type_id = "boundary_in", .role = Role::BoundaryIn};
  static void ports(Ports& p) { p.out<double>("out"); }
  void compute(Slice) override { this->suppress(); }
};

class BoundaryOutFloat : public NodeBase<BoundaryOutFloat> {
 public:
  static constexpr Meta meta{.type_id = "boundary_out", .role = Role::BoundaryOut};
  static void ports(Ports& p) { p.in<double>("in"); }
  void compute(Slice) override { this->suppress(); }
};

class BoundaryInBuffer : public NodeBase<BoundaryInBuffer> {
 public:
  static constexpr Meta meta{.type_id = "boundary_in", .role = Role::BoundaryIn};
  static void ports(Ports& p) { p.out_buffer("out"); }
  void compute(Slice) override { this->suppress(); }
};

class BoundaryOutBuffer : public NodeBase<BoundaryOutBuffer> {
 public:
  static constexpr Meta meta{.type_id = "boundary_out", .role = Role::BoundaryOut};
  static void ports(Ports& p) { p.in_buffer("in"); }
  void compute(Slice) override { this->suppress(); }
};

class BoundaryInBool : public NodeBase<BoundaryInBool> {
 public:
  static constexpr Meta meta{.type_id = "boundary_in", .role = Role::BoundaryIn};
  static void ports(Ports& p) { p.out<bool>("out"); }
  void compute(Slice) override { this->suppress(); }
};

class BoundaryOutBool : public NodeBase<BoundaryOutBool> {
 public:
  static constexpr Meta meta{.type_id = "boundary_out", .role = Role::BoundaryOut};
  static void ports(Ports& p) { p.in<bool>("in"); }
  void compute(Slice) override { this->suppress(); }
};

class BoundaryInString : public NodeBase<BoundaryInString> {
 public:
  static constexpr Meta meta{.type_id = "boundary_in", .role = Role::BoundaryIn};
  static void ports(Ports& p) { p.out<std::string>("out"); }
  void compute(Slice) override { this->suppress(); }
};

class BoundaryOutString : public NodeBase<BoundaryOutString> {
 public:
  static constexpr Meta meta{.type_id = "boundary_out", .role = Role::BoundaryOut};
  static void ports(Ports& p) { p.in<std::string>("in"); }
  void compute(Slice) override { this->suppress(); }
};

inline std::unique_ptr<Node> make_boundary_in(TypeId t = TypeId::Float) {
  switch (t) {
    case TypeId::FloatBuffer:
      return std::make_unique<BoundaryInBuffer>();
    case TypeId::Bool:
      return std::make_unique<BoundaryInBool>();
    case TypeId::String:
      return std::make_unique<BoundaryInString>();
    default:
      return std::make_unique<BoundaryInFloat>();
  }
}

inline std::unique_ptr<Node> make_boundary_out(TypeId t = TypeId::Float) {
  switch (t) {
    case TypeId::FloatBuffer:
      return std::make_unique<BoundaryOutBuffer>();
    case TypeId::Bool:
      return std::make_unique<BoundaryOutBool>();
    case TypeId::String:
      return std::make_unique<BoundaryOutString>();
    default:
      return std::make_unique<BoundaryOutFloat>();
  }
}

}  // namespace node_engine::nodes
