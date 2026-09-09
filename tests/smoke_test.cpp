#include "node_engine/factory.hpp"
#include "node_engine/value.hpp"

#include <cstdlib>
#include <iostream>

int main() {
  using node_engine::TypeId;
  using node_engine::Value;

  Value v = 1.0;
  if (!std::holds_alternative<double>(v)) {
    std::cerr << "Value variant smoke failed\n";
    return EXIT_FAILURE;
  }

  auto& factory = node_engine::NodeFactory::instance();
  (void)factory;
  (void)TypeId::Float;

  std::cout << "smoke_test ok\n";
  return EXIT_SUCCESS;
}
