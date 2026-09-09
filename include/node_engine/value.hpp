#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace node_engine {

enum class TypeId {
  Float,
  Bool,
  String,
  FloatBuffer,
  Dynamic,
};

using Value = std::variant<
    std::monostate,
    double,
    bool,
    std::string,
    std::vector<double>>;

}  // namespace node_engine
