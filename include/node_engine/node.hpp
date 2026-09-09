#pragma once

#include "node_engine/pin.hpp"

#include <cstddef>
#include <string>
#include <string_view>
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

class Node {
 public:
  virtual ~Node() = default;

  virtual std::string_view type_id() const = 0;

  // Single entry point — engine always calls this
  // (scalar, full buffer, or one parallel chunk).
  virtual void compute(Slice slice) = 0;

  virtual ParallelHint parallel_hint() const { return {}; }
  virtual std::size_t parallel_length() const { return 0; }
  virtual void propagate_types() {}

  std::vector<Pin> inputs;
  std::vector<Pin> outputs;
  bool dirty = true;
};

}  // namespace node_engine
