#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace node_engine::nodes::line_quality {

inline constexpr std::size_t kChannels = 24;

inline std::size_t num_frames(std::vector<double> const& buf) {
  if (buf.size() % kChannels != 0) {
    throw std::runtime_error("FloatBuffer size must be multiple of 24 (frame channels)");
  }
  return buf.size() / kChannels;
}

inline double const* frame_ptr(std::vector<double> const& buf, std::size_t frame_i) {
  return buf.data() + frame_i * kChannels;
}

inline double* frame_ptr(std::vector<double>& buf, std::size_t frame_i) {
  return buf.data() + frame_i * kChannels;
}

}  // namespace node_engine::nodes::line_quality
