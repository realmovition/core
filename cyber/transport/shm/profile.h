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

#ifndef CYBER_TRANSPORT_SHM_PROFILE_H_
#define CYBER_TRANSPORT_SHM_PROFILE_H_

#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#include "cyber/common/global_data.h"

namespace apollo {
namespace cyber {
namespace transport {

class TransportProfileRecorder {
 public:
  static TransportProfileRecorder* Instance() {
    static auto* instance = new TransportProfileRecorder();
    return instance;
  }

  void Record(uint64_t channel_id, std::size_t payload_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& stats = stats_[channel_id];
    ++stats.sample_count;
    if (payload_size > stats.max_payload_size) {
      stats.max_payload_size = payload_size;
    }
  }

  void RecordBusy(uint64_t channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_[channel_id].write_busy_count;
  }

  std::string GenerateToml() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream os;
    os << "# auto-generated transport profile\n";
    for (const auto& item : stats_) {
      const auto& channel = item.first;
      const auto& stats = item.second;
      os << "[[channel]]\n";
      os << "name = \"" << common::GlobalData::GetChannelById(channel) << "\"\n";
      os << "channel_id = " << channel << "\n";
      os << "max_payload_size = " << stats.max_payload_size << "\n";
      os << "sample_count = " << stats.sample_count << "\n";
      os << "write_busy_count = " << stats.write_busy_count << "\n";
      os << "\n";
    }
    return os.str();
  }

  bool DumpToml(const std::string& path) const {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    out << GenerateToml();
    return out.good();
  }

 private:
  struct Stats {
    std::size_t max_payload_size = 0;
    std::uint64_t sample_count = 0;
    std::uint64_t write_busy_count = 0;
  };

  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, Stats> stats_;
};

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_TRANSPORT_SHM_PROFILE_H_
