#include "node_engine/trigger_queue.hpp"

namespace node_engine {

void TriggerQueue::push(std::string node_id) {
  {
    std::lock_guard lock(mutex_);
    queue_.push(std::move(node_id));
  }
  cv_.notify_one();
}

std::string TriggerQueue::wait_pop() {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return !queue_.empty(); });
  auto id = std::move(queue_.front());
  queue_.pop();
  return id;
}

std::optional<std::string> TriggerQueue::try_pop() {
  std::lock_guard lock(mutex_);
  if (queue_.empty()) {
    return std::nullopt;
  }
  auto id = std::move(queue_.front());
  queue_.pop();
  return id;
}

bool TriggerQueue::empty() const {
  std::lock_guard lock(mutex_);
  return queue_.empty();
}

}  // namespace node_engine
