#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

namespace node_engine {

// Engine-owned inbound trigger queue. Producers push node ids;
// main loop waits/pops, marks dirty, runs a pass.
class TriggerQueue {
 public:
  void push(std::string node_id);

  // Blocks until a trigger is available.
  std::string wait_pop();

  // Non-blocking pop.
  std::optional<std::string> try_pop();

  [[nodiscard]] bool empty() const;

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::string> queue_;
};

}  // namespace node_engine
