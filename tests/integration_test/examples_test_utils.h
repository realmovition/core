// Copyright 2025 WheelOS. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//  Created Date: 2026-05-31
//  Author: daohu527

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>

#include "cyber/cyber.h"

namespace apollo {
namespace cyber {
namespace examples {
namespace test {

constexpr uint64_t kWarmupSeqBase = std::numeric_limits<uint64_t>::max() - 1024;

inline std::string UniqueName(const std::string& prefix) {
  static std::atomic<uint64_t> counter{0};
  return prefix + "_" + std::to_string(counter.fetch_add(1));
}

template <typename Predicate>
inline bool WaitFor(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

inline ReaderConfig MakeReaderConfig(const std::string& channel_name,
                                     uint32_t queue_depth) {
  ReaderConfig config;
  config.channel_name = channel_name;
  config.pending_queue_size = queue_depth;
  config.qos_profile.set_depth(queue_depth);
  return config;
}

inline std::string MakePayload(size_t size, uint8_t seed) {
  std::string payload(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    payload[i] = static_cast<char>(((seed + i * 37U) % 251U) + 1U);
  }
  if (!payload.empty()) {
    payload[0] = static_cast<char>(seed + 1U);
  }
  if (payload.size() > 2) {
    payload[payload.size() / 2] = '\0';
  }
  if (payload.size() > 4) {
    payload.back() = '\0';
  }
  return payload;
}

}  // namespace test
}  // namespace examples
}  // namespace cyber
}  // namespace apollo
