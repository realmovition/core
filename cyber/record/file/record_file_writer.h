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

#include <liburing.h>
#include <sys/uio.h>

#include <array>
#include <atomic>
#include <chrono>
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

struct ChunkBuffer {
  void Clear() {
    begin_time = 0;
    end_time = 0;
    message_number = 0;
    raw_size = 0;
    body_bytes.clear();
  }
  bool Empty() const { return message_number == 0; }

  uint64_t begin_time = 0;
  uint64_t end_time = 0;
  uint64_t message_number = 0;
  uint64_t raw_size = 0;
  std::vector<char> body_bytes;
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
  struct MessageSlot {
    std::string channel_name;
    uint64_t time_ns = 0;
    std::array<char, 256> inline_payload = {};
    std::unique_ptr<char[]> heap_payload;
    size_t payload_size = 0;
    size_t payload_capacity = inline_payload.size();
    bool use_inline_payload = true;
    int next_free = -1;
    bool Store(const proto::SingleMessage& message);
    const char* PayloadData() const {
      return use_inline_payload ? inline_payload.data() : heap_payload.get();
    }
    void Reset() {
      channel_name.clear();
      time_ns = 0;
      payload_size = 0;
      use_inline_payload = true;
    }
  };

  class SlotPool {
   public:
    bool Init(size_t capacity);
    int Acquire();
    void Release(int index);
    MessageSlot* At(int index);

   private:
    std::vector<MessageSlot> slots_;
    std::atomic<int> free_head_{-1};
  };

  class BoundedMpscRing {
   public:
    bool Init(size_t capacity);
    bool Push(int slot_index);
    bool Pop(int* slot_index);
    bool Empty() const;

   private:
    size_t capacity_ = 0;
    size_t mask_ = 0;
    std::unique_ptr<std::atomic<int>[]> data_;
    std::atomic<uint64_t> head_{0};
    std::atomic<uint64_t> tail_{0};
  };

  struct InflightWrite {
    enum class OpKind { kWritev, kWriteFixed };
    OpKind op_kind = OpKind::kWritev;
    Section section_a = {};
    Section section_b = {};
    std::vector<char> payload_a;
    std::vector<char> payload_b;
    std::array<struct iovec, 4> iov = {};
    int iovcnt = 0;
    size_t expected = 0;
    int chunk_buffer_index = -1;
    size_t fixed_nbytes = 0;
    int fixed_buf_index = -1;
  };

  struct ChunkWriteBuffer {
    enum class State { FREE, FILLING, SUBMITTED };
    std::vector<char> storage;
    size_t used = 0;
    uint64_t message_number = 0;
    proto::ChunkHeader header;
    State state = State::FREE;
  };

  bool WriteChunk(Chunk* chunk);
  template <typename T>
  bool WriteSection(const T& message);
  bool WriteSectionRaw(proto::SectionType type, std::vector<char>&& payload);
  bool WriteChunkBodyFromFixedBuffer(int buffer_index);
  int AcquireChunkWriteBufferLocked();
  void InitChunkBuffers();
  void ResetChunkBuffers();
  bool WriteIndex();
  void BackendLoop();

  bool InitIoUring();
  bool SubmitWritev(std::unique_ptr<InflightWrite> request, uint64_t offset);
  bool SubmitWriteFixed(std::unique_ptr<InflightWrite> request, uint64_t offset);
  bool PollCompletions(bool wait_for_one, bool* completed);
  bool FlushCompletions();
  void SubmitBatchIfNeeded(bool force_submit);
  bool SubmitHeaderRewrite(const proto::Header& header);
  uint64_t CurrentPosition() const { return logical_position_; }
  bool EnqueueMessage(const proto::SingleMessage& message);
  void HandleDrop(uint64_t payload_bytes);
  bool FlushActiveChunk();
  bool FlushActiveChunkLocked();
  void ResetBatchStateLocked();

  std::atomic_bool is_writing_{false};
  std::thread backend_thread_;
  mutable std::mutex queue_wait_mutex_;
  std::condition_variable queue_wait_cv_;

  SlotPool slot_pool_;
  BoundedMpscRing queue_;
  Chunk chunk_active_;
  std::atomic<uint64_t> dropped_frames_{0};
  std::atomic<uint64_t> dropped_bytes_{0};
  std::atomic<uint64_t> dropped_since_alert_{0};

  std::unordered_map<std::string, uint64_t> channel_message_number_map_;
  mutable std::mutex channel_map_mutex_;

  struct io_uring ring_ = {};
  bool ring_initialized_ = false;
  bool ring_fixed_file_registered_ = false;
  bool ring_fixed_buffers_registered_ = false;
  bool io_failed_ = false;
  int in_flight_io_ = 0;
  int pending_submit_count_ = 0;
  uint64_t logical_position_ = 0;
  uint64_t next_disk_offset_ = 0;
  mutable std::mutex io_mutex_;
  uint64_t batch_message_count_ = 0;
  uint64_t batch_payload_bytes_ = 0;
  std::chrono::steady_clock::time_point batch_start_time_;
  bool batch_open_ = false;
  std::vector<ChunkWriteBuffer> chunk_buffers_;
  std::vector<struct iovec> registered_iovecs_;

  static constexpr size_t kQueueCapacity = 16384;
  static constexpr uint64_t kDropAlertEveryFrames = 100;
  static constexpr int kRingDepth = 128;
  static constexpr int kSubmitBatch = 16;
  static constexpr int kChunkBufferCount = 8;
  static constexpr size_t kChunkBufferSize = 2 * 1024 * 1024;
  static constexpr int kMaxInflightChunks = 8;
  static constexpr uint64_t kMaxBatchMsgs = 64;
  static constexpr uint64_t kMaxBatchBytes = 2 * 1024 * 1024;
  static constexpr uint64_t kMaxBatchWaitUs = 20000;
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
