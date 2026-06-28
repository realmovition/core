/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#ifndef CYBER_RECORD_FILE_RECORD_FILE_WRITER_H_
#define CYBER_RECORD_FILE_RECORD_FILE_WRITER_H_

#include <sys/uio.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "google/protobuf/message.h"

#include "cyber/common/log.h"
#include "cyber/record/file/record_file_base.h"
#include "cyber/record/file/section.h"

namespace apollo {
namespace cyber {
namespace record {

struct Chunk {
  Chunk();
  void clear();
  void add(const proto::SingleMessage& message);
  void add(const std::string& channel_name, uint64_t time_ns, const char* payload,
           size_t payload_size);
  bool empty() const;

  proto::ChunkHeader header_;
  std::vector<char> body_bytes_;
};

class RecordFileWriter : public RecordFileBase {
 public:
  RecordFileWriter();
  virtual ~RecordFileWriter();
  bool Open(const std::string& path) override;
  void Close() override;
  bool WriteHeader(const proto::Header& header);
  bool WriteChannel(const proto::Channel& channel);
  bool WriteMessage(const proto::SingleMessage& message);
  uint64_t GetMessageNumber(const std::string& channel_name) const;

 private:
  bool WriteChunk(Chunk* chunk);
  template <typename T>
  bool WriteSection(const T& message);
  bool WriteSectionRaw(proto::SectionType type, std::vector<char>&& payload);
  bool WriteBuffers(const struct iovec* iov, int iovcnt, size_t expected,
                    uint64_t offset);
  bool WriteHeaderRewrite(const proto::Header& header);
  bool RotateActiveChunkLocked(std::unique_lock<std::mutex>* state_lock);
  bool WriteIndex();
  void BackendLoop();
  uint64_t CurrentPosition() const { return logical_position_; }
  bool EnsureFilePreallocatedLocked(uint64_t required_size_bytes);
  bool MaybeDataSyncLocked(bool force);
  void LoadRuntimeTuningFromEnv();

  std::atomic_bool is_writing_{false};
  std::atomic_bool io_failed_{false};
  std::thread backend_thread_;

  mutable std::mutex state_mutex_;
  std::condition_variable backend_cv_;
  std::condition_variable flush_slot_cv_;
  std::unique_ptr<Chunk> active_chunk_;
  std::unique_ptr<Chunk> flush_chunk_;
  bool flush_pending_ = false;

  std::unordered_map<std::string, uint64_t> channel_message_number_map_;
  mutable std::mutex channel_map_mutex_;

  mutable std::mutex io_mutex_;
  uint64_t logical_position_ = 0;
  uint64_t chunk_interval_ns_ = 0;
  uint64_t chunk_raw_size_bytes_ = 0;
  uint64_t prealloc_step_bytes_ = 0;
  uint64_t preallocated_size_bytes_ = 0;
  uint64_t fdatasync_interval_bytes_ = 0;
  uint64_t unsynced_bytes_ = 0;
  bool prealloc_supported_ = true;
  bool prealloc_warned_ = false;

  static constexpr size_t kInitialBodyReserveBytes = 256 * 1024;
  static constexpr uint64_t kDefaultPreallocStepBytes = 256ULL * 1024 * 1024;
};

template <typename T>
bool RecordFileWriter::WriteSection(const T& message) {
  proto::SectionType type;
  if (std::is_same<T, proto::ChunkHeader>::value) {
    type = proto::SectionType::SECTION_CHUNK_HEADER;
  } else if (std::is_same<T, proto::ChunkBody>::value) {
    type = proto::SectionType::SECTION_CHUNK_BODY;
  } else if (std::is_same<T, proto::Channel>::value) {
    type = proto::SectionType::SECTION_CHANNEL;
  } else if (std::is_same<T, proto::Index>::value) {
    type = proto::SectionType::SECTION_INDEX;
  } else {
    AERROR << "Do not support this template typename.";
    return false;
  }

  std::vector<char> payload(message.ByteSizeLong());
  if (!message.SerializeToArray(payload.data(), static_cast<int>(payload.size()))) {
    AERROR << "Serialize section payload failed, type: " << type;
    return false;
  }
  return WriteSectionRaw(type, std::move(payload));
}

}  // namespace record
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_RECORD_FILE_RECORD_FILE_WRITER_H_
