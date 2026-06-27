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

#include "cyber/record/file/record_file_writer.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>
#include <utility>

#include "cyber/common/file.h"
#include "cyber/task/task.h"

namespace apollo {
namespace cyber {
namespace record {

using apollo::cyber::proto::Channel;
using apollo::cyber::proto::ChannelCache;
using apollo::cyber::proto::ChunkBodyCache;
using apollo::cyber::proto::ChunkHeader;
using apollo::cyber::proto::ChunkHeaderCache;
using apollo::cyber::proto::Header;
using apollo::cyber::proto::SectionType;
using apollo::cyber::proto::SingleIndex;

namespace {
constexpr int kInvalidSlot = -1;
constexpr int kSpinningIterations = 256;

size_t RoundPayloadCapacity(size_t payload_size) {
  if (payload_size <= 256) {
    return 256;
  }
  if (payload_size <= 4 * 1024) {
    return 4 * 1024;
  }
  if (payload_size <= 64 * 1024) {
    return 64 * 1024;
  }
  if (payload_size <= 1024 * 1024) {
    return 1024 * 1024;
  }
  if (payload_size <= 4 * 1024 * 1024) {
    return 4 * 1024 * 1024;
  }
  return payload_size;
}

size_t VarintSize(uint64_t value) {
  size_t size = 1;
  while (value >= 0x80) {
    value >>= 7;
    ++size;
  }
  return size;
}

size_t EncodeVarint(char* out, uint64_t value) {
  size_t written = 0;
  while (value >= 0x80) {
    out[written++] = static_cast<char>((value & 0x7F) | 0x80);
    value >>= 7;
  }
  out[written++] = static_cast<char>(value);
  return written;
}

void AppendSingleMessageToChunkBody(std::vector<char>* body_bytes,
                                    const std::string& channel_name, uint64_t time_ns,
                                    const char* payload, size_t payload_size) {
  const size_t channel_size = channel_name.size();
  const size_t single_message_size =
      1 + VarintSize(channel_size) + channel_size +
      1 + VarintSize(time_ns) +
      1 + VarintSize(payload_size) + payload_size;
  const size_t chunk_entry_size =
      1 + VarintSize(single_message_size) + single_message_size;
  const size_t old_size = body_bytes->size();
  body_bytes->resize(old_size + chunk_entry_size);
  char* out = body_bytes->data() + old_size;

  // ChunkBody.messages (field 1): length-delimited SingleMessage bytes.
  *out++ = static_cast<char>(0x0A);
  out += EncodeVarint(out, static_cast<uint64_t>(single_message_size));

  // SingleMessage.channel_name (field 1): length-delimited string.
  *out++ = static_cast<char>(0x0A);
  out += EncodeVarint(out, static_cast<uint64_t>(channel_size));
  if (channel_size > 0) {
    memcpy(out, channel_name.data(), channel_size);
    out += channel_size;
  }

  // SingleMessage.time (field 2): uint64 varint.
  *out++ = static_cast<char>(0x10);
  out += EncodeVarint(out, time_ns);

  // SingleMessage.content (field 3): length-delimited bytes.
  *out++ = static_cast<char>(0x1A);
  out += EncodeVarint(out, static_cast<uint64_t>(payload_size));
  if (payload_size > 0) {
    memcpy(out, payload, payload_size);
    out += payload_size;
  }
}

}  // namespace

Chunk::Chunk() { clear(); }

void Chunk::clear() {
  header_.set_begin_time(0);
  header_.set_end_time(0);
  header_.set_message_number(0);
  header_.set_raw_size(0);
  body_bytes_.clear();
}

void Chunk::add(const proto::SingleMessage& message) {
  add(message.channel_name(), message.time(), message.content().data(),
      message.content().size());
}

void Chunk::add(const std::string& channel_name, uint64_t time_ns, const char* payload,
                size_t payload_size) {
  AppendSingleMessageToChunkBody(&body_bytes_, channel_name, time_ns, payload, payload_size);
  if (header_.begin_time() == 0 || header_.begin_time() > time_ns) {
    header_.set_begin_time(time_ns);
  }
  if (header_.end_time() < time_ns) {
    header_.set_end_time(time_ns);
  }
  header_.set_message_number(header_.message_number() + 1);
  header_.set_raw_size(header_.raw_size() + payload_size);
}

bool Chunk::empty() const { return header_.message_number() == 0; }

bool RecordFileWriter::MessageSlot::Store(const proto::SingleMessage& message) {
  channel_name = message.channel_name();
  time_ns = message.time();
  payload_size = message.content().size();
  if (payload_size <= inline_payload.size()) {
    use_inline_payload = true;
    payload_capacity = inline_payload.size();
    if (payload_size > 0) {
      memcpy(inline_payload.data(), message.content().data(), payload_size);
    }
    return true;
  }
  use_inline_payload = false;
  const size_t target_capacity = RoundPayloadCapacity(payload_size);
  if (!heap_payload || payload_capacity < target_capacity) {
    heap_payload.reset(new (std::nothrow) char[target_capacity]);
    if (!heap_payload) {
      return false;
    }
    payload_capacity = target_capacity;
  }
  memcpy(heap_payload.get(), message.content().data(), payload_size);
  return true;
}

bool RecordFileWriter::SlotPool::Init(size_t capacity) {
  slots_.resize(capacity);
  if (slots_.empty()) {
    return false;
  }
  for (int i = static_cast<int>(slots_.size()) - 1; i >= 0; --i) {
    slots_[i].next_free = (i == static_cast<int>(slots_.size()) - 1) ? -1 : i + 1;
  }
  free_head_.store(0, std::memory_order_release);
  return true;
}

int RecordFileWriter::SlotPool::Acquire() {
  int head = free_head_.load(std::memory_order_acquire);
  while (head != -1) {
    const int next = slots_[head].next_free;
    if (free_head_.compare_exchange_weak(head, next, std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
      return head;
    }
  }
  return -1;
}

void RecordFileWriter::SlotPool::Release(int index) {
  if (index < 0 || index >= static_cast<int>(slots_.size())) {
    return;
  }
  int head = free_head_.load(std::memory_order_acquire);
  do {
    slots_[index].next_free = head;
  } while (!free_head_.compare_exchange_weak(head, index, std::memory_order_acq_rel,
                                             std::memory_order_acquire));
}

RecordFileWriter::MessageSlot* RecordFileWriter::SlotPool::At(int index) {
  if (index < 0 || index >= static_cast<int>(slots_.size())) {
    return nullptr;
  }
  return &slots_[index];
}

bool RecordFileWriter::BoundedMpscRing::Init(size_t capacity) {
  if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
    return false;
  }
  capacity_ = capacity;
  mask_ = capacity - 1;
  data_.reset(new std::atomic<int>[capacity_]);
  for (size_t i = 0; i < capacity_; ++i) {
    data_[i].store(kInvalidSlot, std::memory_order_relaxed);
  }
  head_.store(0, std::memory_order_release);
  tail_.store(0, std::memory_order_release);
  return true;
}

bool RecordFileWriter::BoundedMpscRing::Push(int slot_index) {
  while (true) {
    uint64_t tail = tail_.load(std::memory_order_relaxed);
    const uint64_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= capacity_) {
      return false;
    }
    if (!tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_acq_rel,
                                     std::memory_order_relaxed)) {
      continue;
    }
    std::atomic<int>& cell = data_[tail & mask_];
    int expected = kInvalidSlot;
    int spin = 0;
    while (!cell.compare_exchange_weak(expected, slot_index, std::memory_order_release,
                                       std::memory_order_relaxed)) {
      expected = kInvalidSlot;
      if (++spin >= kSpinningIterations) {
        apollo::cyber::Yield();
        spin = 0;
      }
    }
    return true;
  }
}

bool RecordFileWriter::BoundedMpscRing::Pop(int* slot_index) {
  const uint64_t head = head_.load(std::memory_order_relaxed);
  const uint64_t tail = tail_.load(std::memory_order_acquire);
  if (head >= tail) {
    return false;
  }

  std::atomic<int>& cell = data_[head & mask_];
  int value = cell.load(std::memory_order_acquire);
  int spin = 0;
  while (value == kInvalidSlot) {
    if (++spin >= kSpinningIterations) {
      apollo::cyber::Yield();
      spin = 0;
    }
    value = cell.load(std::memory_order_acquire);
  }
  cell.store(kInvalidSlot, std::memory_order_release);
  head_.store(head + 1, std::memory_order_release);
  *slot_index = value;
  return true;
}

bool RecordFileWriter::BoundedMpscRing::Empty() const {
  return head_.load(std::memory_order_acquire) >=
         tail_.load(std::memory_order_acquire);
}

RecordFileWriter::RecordFileWriter() = default;

RecordFileWriter::~RecordFileWriter() { Close(); }

bool RecordFileWriter::InitIoUring() {
  if (io_uring_queue_init(kRingDepth, &ring_, 0) < 0) {
    AERROR << "io_uring_queue_init failed, file: " << path_;
    return false;
  }
  ring_initialized_ = true;
  ring_fixed_file_registered_ = false;
  ring_fixed_buffers_registered_ = false;
  io_failed_ = false;
  in_flight_io_ = 0;
  pending_submit_count_ = 0;

  int fixed_fd = fd_;
  if (io_uring_register_files(&ring_, &fixed_fd, 1) == 0) {
    ring_fixed_file_registered_ = true;
  } else {
    AWARN << "io_uring_register_files failed, fallback to normal fd mode.";
  }

  registered_iovecs_.clear();
  if (!chunk_buffers_.empty()) {
    registered_iovecs_.resize(chunk_buffers_.size());
    for (size_t i = 0; i < chunk_buffers_.size(); ++i) {
      registered_iovecs_[i].iov_base = chunk_buffers_[i].storage.data();
      registered_iovecs_[i].iov_len = chunk_buffers_[i].storage.size();
    }
    if (io_uring_register_buffers(&ring_, registered_iovecs_.data(),
                                  static_cast<unsigned int>(registered_iovecs_.size())) == 0) {
      ring_fixed_buffers_registered_ = true;
    } else {
      registered_iovecs_.clear();
      AWARN << "io_uring_register_buffers failed, fallback to normal writev.";
    }
  }
  return true;
}

bool RecordFileWriter::Open(const std::string& path) {
  std::lock_guard<std::mutex> io_lock(io_mutex_);
  path_ = path;
  if (::apollo::cyber::common::PathExists(path_)) {
    AWARN << "File exist and overwrite, file: " << path_;
  }
  fd_ = open(path_.data(), O_CREAT | O_WRONLY | O_TRUNC,
             S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd_ < 0) {
    AERROR << "Open file failed, file: " << path_ << ", fd: " << fd_
           << ", errno: " << errno;
    return false;
  }

  if (!slot_pool_.Init(kQueueCapacity) || !queue_.Init(kQueueCapacity)) {
    AERROR << "Init writer queue/pool failed.";
    close(fd_);
    fd_ = -1;
    return false;
  }

  logical_position_ = 0;
  next_disk_offset_ = 0;
  chunk_active_.clear();
  InitChunkBuffers();
  if (!InitIoUring()) {
    close(fd_);
    fd_ = -1;
    return false;
  }
  batch_message_count_ = 0;
  batch_payload_bytes_ = 0;
  batch_open_ = false;
  dropped_frames_.store(0, std::memory_order_release);
  dropped_bytes_.store(0, std::memory_order_release);
  dropped_since_alert_.store(0, std::memory_order_release);
  is_writing_.store(true, std::memory_order_release);
  backend_thread_ = std::thread([this]() { BackendLoop(); });
  return true;
}

void RecordFileWriter::InitChunkBuffers() {
  chunk_buffers_.clear();
  chunk_buffers_.resize(kChunkBufferCount);
  for (auto& buffer : chunk_buffers_) {
    buffer.storage.resize(kChunkBufferSize);
    buffer.used = 0;
    buffer.message_number = 0;
    buffer.header.Clear();
    buffer.state = ChunkWriteBuffer::State::FREE;
  }
}

void RecordFileWriter::ResetChunkBuffers() {
  for (auto& buffer : chunk_buffers_) {
    buffer.used = 0;
    buffer.message_number = 0;
    buffer.header.Clear();
    buffer.state = ChunkWriteBuffer::State::FREE;
  }
}

void RecordFileWriter::SubmitBatchIfNeeded(bool force_submit) {
  if (!ring_initialized_) {
    return;
  }
  if (!force_submit && pending_submit_count_ < kSubmitBatch) {
    return;
  }
  if (pending_submit_count_ <= 0) {
    return;
  }
  if (io_uring_submit(&ring_) < 0) {
    io_failed_ = true;
    AERROR << "io_uring_submit failed, file: " << path_;
  }
  pending_submit_count_ = 0;
}

bool RecordFileWriter::PollCompletions(bool wait_for_one, bool* completed) {
  *completed = false;
  if (!ring_initialized_) {
    return false;
  }
  struct io_uring_cqe* cqe = nullptr;
  if (wait_for_one && in_flight_io_ > 0) {
    SubmitBatchIfNeeded(true);
    if (io_uring_wait_cqe(&ring_, &cqe) < 0 || cqe == nullptr) {
      io_failed_ = true;
      AERROR << "io_uring_wait_cqe failed, file: " << path_;
      return false;
    }
  }
  while (true) {
    if (cqe == nullptr && io_uring_peek_cqe(&ring_, &cqe) != 0) {
      break;
    }
    *completed = true;
    auto* request =
        reinterpret_cast<std::unique_ptr<InflightWrite>*>(io_uring_cqe_get_data(cqe));
    const int res = cqe->res;
    if (request != nullptr) {
      const int chunk_buffer_index = (*request)->chunk_buffer_index;
      const size_t expected = (*request)->expected;
      if (res < 0 || static_cast<size_t>(res) != expected) {
        io_failed_ = true;
        AERROR << "io_uring completion failed, file: " << path_
               << ", expect: " << expected << ", res: " << res;
      }
      if (chunk_buffer_index >= 0 &&
          chunk_buffer_index < static_cast<int>(chunk_buffers_.size())) {
        auto& buffer = chunk_buffers_[chunk_buffer_index];
        buffer.state = ChunkWriteBuffer::State::FREE;
        buffer.used = 0;
        buffer.message_number = 0;
        buffer.header.Clear();
      }
      delete request;
    }
    io_uring_cqe_seen(&ring_, cqe);
    cqe = nullptr;
    --in_flight_io_;
  }
  return !io_failed_;
}

bool RecordFileWriter::FlushCompletions() {
  SubmitBatchIfNeeded(true);
  while (in_flight_io_ > 0) {
    bool completed = false;
    if (!PollCompletions(true, &completed)) {
      return false;
    }
  }
  return !io_failed_;
}

bool RecordFileWriter::SubmitWritev(std::unique_ptr<InflightWrite> request,
                                    uint64_t offset) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  while (sqe == nullptr) {
    bool completed = false;
    if (!PollCompletions(true, &completed)) {
      return false;
    }
    sqe = io_uring_get_sqe(&ring_);
  }
  auto* request_holder = new std::unique_ptr<InflightWrite>(std::move(request));
  const int submit_fd = ring_fixed_file_registered_ ? 0 : fd_;
  io_uring_prep_writev(sqe, submit_fd, (*request_holder)->iov.data(),
                       (*request_holder)->iovcnt, offset);
  if (ring_fixed_file_registered_) {
    sqe->flags |= IOSQE_FIXED_FILE;
  }
  (*request_holder)->op_kind = InflightWrite::OpKind::kWritev;
  io_uring_sqe_set_data(sqe, request_holder);
  ++in_flight_io_;
  ++pending_submit_count_;
  SubmitBatchIfNeeded(false);
  return !io_failed_;
}

bool RecordFileWriter::SubmitWriteFixed(std::unique_ptr<InflightWrite> request,
                                        uint64_t offset) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  while (sqe == nullptr) {
    bool completed = false;
    if (!PollCompletions(true, &completed)) {
      return false;
    }
    sqe = io_uring_get_sqe(&ring_);
  }
  auto* request_holder = new std::unique_ptr<InflightWrite>(std::move(request));
  const int submit_fd = ring_fixed_file_registered_ ? 0 : fd_;
  auto* fixed_data = chunk_buffers_[(*request_holder)->fixed_buf_index].storage.data();
  io_uring_prep_write_fixed(sqe, submit_fd, fixed_data,
                            static_cast<unsigned int>((*request_holder)->fixed_nbytes),
                            offset, (*request_holder)->fixed_buf_index);
  if (ring_fixed_file_registered_) {
    sqe->flags |= IOSQE_FIXED_FILE;
  }
  (*request_holder)->op_kind = InflightWrite::OpKind::kWriteFixed;
  io_uring_sqe_set_data(sqe, request_holder);
  ++in_flight_io_;
  ++pending_submit_count_;
  SubmitBatchIfNeeded(false);
  return !io_failed_;
}

bool RecordFileWriter::SubmitHeaderRewrite(const proto::Header& header) {
  auto request = std::unique_ptr<InflightWrite>(new InflightWrite());
  const size_t message_size = header.ByteSizeLong();
  if (message_size > HEADER_LENGTH) {
    AERROR << "Header size exceeds reserved length, size: " << message_size
           << ", max: " << HEADER_LENGTH;
    return false;
  }
  request->section_a = {proto::SectionType::SECTION_HEADER,
                        static_cast<int64_t>(message_size)};
  request->payload_a.resize(message_size);
  if (!header.SerializeToArray(request->payload_a.data(),
                               static_cast<int>(request->payload_a.size()))) {
    AERROR << "Serialize header failed.";
    return false;
  }
  request->payload_b.assign(HEADER_LENGTH - message_size, '0');
  request->iov[0].iov_base = &request->section_a;
  request->iov[0].iov_len = sizeof(Section);
  request->iov[1].iov_base = request->payload_a.data();
  request->iov[1].iov_len = request->payload_a.size();
  request->iov[2].iov_base = request->payload_b.data();
  request->iov[2].iov_len = request->payload_b.size();
  request->iovcnt = 3;
  request->expected = sizeof(Section) + HEADER_LENGTH;
  return SubmitWritev(std::move(request), 0);
}

bool RecordFileWriter::WriteSectionRaw(proto::SectionType type,
                                       std::vector<char>&& payload) {
  const uint64_t payload_size = static_cast<uint64_t>(payload.size());
  auto request = std::unique_ptr<InflightWrite>(new InflightWrite());
  request->section_a = {type, static_cast<int64_t>(payload_size)};
  request->payload_a = std::move(payload);
  request->iov[0].iov_base = &request->section_a;
  request->iov[0].iov_len = sizeof(Section);
  request->iov[1].iov_base = request->payload_a.data();
  request->iov[1].iov_len = request->payload_a.size();
  request->iovcnt = 2;
  request->expected = sizeof(Section) + request->payload_a.size();

  const uint64_t offset = next_disk_offset_;
  if (!SubmitWritev(std::move(request), offset)) {
    return false;
  }
  next_disk_offset_ += sizeof(Section) + payload_size;
  logical_position_ += sizeof(Section) + payload_size;
  header_.set_size(logical_position_);
  return true;
}

bool RecordFileWriter::WriteChunkBodyFromFixedBuffer(int buffer_index) {
  if (buffer_index < 0 || buffer_index >= static_cast<int>(chunk_buffers_.size())) {
    return false;
  }
  auto& buffer = chunk_buffers_[buffer_index];
  if (buffer.state != ChunkWriteBuffer::State::FILLING || buffer.used == 0) {
    return false;
  }

  const uint64_t offset = next_disk_offset_;
  auto section_request = std::unique_ptr<InflightWrite>(new InflightWrite());
  section_request->section_a = {proto::SectionType::SECTION_CHUNK_BODY,
                                static_cast<int64_t>(buffer.used)};
  section_request->iov[0].iov_base = &section_request->section_a;
  section_request->iov[0].iov_len = sizeof(Section);
  section_request->iovcnt = 1;
  section_request->expected = sizeof(Section);
  if (!SubmitWritev(std::move(section_request), offset)) {
    return false;
  }

  bool submitted_body = false;
  if (ring_fixed_buffers_registered_) {
    auto body_request = std::unique_ptr<InflightWrite>(new InflightWrite());
    body_request->expected = buffer.used;
    body_request->chunk_buffer_index = buffer_index;
    body_request->fixed_nbytes = buffer.used;
    body_request->fixed_buf_index = buffer_index;
    if (SubmitWriteFixed(std::move(body_request), offset + sizeof(Section))) {
      submitted_body = true;
    } else {
      io_failed_ = true;
      return false;
    }
  } else {
    auto body_request = std::unique_ptr<InflightWrite>(new InflightWrite());
    body_request->iov[0].iov_base = buffer.storage.data();
    body_request->iov[0].iov_len = buffer.used;
    body_request->iovcnt = 1;
    body_request->expected = buffer.used;
    body_request->chunk_buffer_index = buffer_index;
    if (SubmitWritev(std::move(body_request), offset + sizeof(Section))) {
      submitted_body = true;
    } else {
      io_failed_ = true;
      return false;
    }
  }

  if (!submitted_body) {
    return false;
  }
  next_disk_offset_ += sizeof(Section) + buffer.used;
  logical_position_ += sizeof(Section) + buffer.used;
  header_.set_size(logical_position_);
  buffer.state = ChunkWriteBuffer::State::SUBMITTED;
  return true;
}

int RecordFileWriter::AcquireChunkWriteBufferLocked() {
  for (int i = 0; i < static_cast<int>(chunk_buffers_.size()); ++i) {
    auto& buffer = chunk_buffers_[i];
    if (buffer.state == ChunkWriteBuffer::State::FREE) {
      buffer.state = ChunkWriteBuffer::State::FILLING;
      return i;
    }
  }
  return -1;
}

bool RecordFileWriter::WriteHeader(const Header& header) {
  std::lock_guard<std::mutex> io_lock(io_mutex_);
  header_ = header;
  if (!SubmitHeaderRewrite(header_)) {
    return false;
  }
  if (logical_position_ == 0) {
    logical_position_ += sizeof(Section) + HEADER_LENGTH;
    next_disk_offset_ = logical_position_;
    header_.set_size(logical_position_);
  }
  return true;
}

bool RecordFileWriter::WriteChannel(const Channel& channel) {
  std::lock_guard<std::mutex> io_lock(io_mutex_);
  uint64_t pos = CurrentPosition();
  if (!WriteSection<Channel>(channel)) {
    AERROR << "Write channel section fail";
    return false;
  }
  header_.set_channel_number(header_.channel_number() + 1);
  SingleIndex* single_index = index_.add_indexes();
  single_index->set_type(SectionType::SECTION_CHANNEL);
  single_index->set_position(pos);
  auto* channel_cache = new ChannelCache();
  channel_cache->set_name(channel.name());
  channel_cache->set_message_number(0);
  channel_cache->set_message_type(channel.message_type());
  channel_cache->set_proto_desc(channel.proto_desc());
  single_index->set_allocated_channel_cache(channel_cache);
  return true;
}

void RecordFileWriter::HandleDrop(uint64_t payload_bytes) {
  dropped_frames_.fetch_add(1, std::memory_order_relaxed);
  dropped_bytes_.fetch_add(payload_bytes, std::memory_order_relaxed);
  const uint64_t local = dropped_since_alert_.fetch_add(1, std::memory_order_relaxed) + 1;
  if (local >= kDropAlertEveryFrames) {
    dropped_since_alert_.store(0, std::memory_order_relaxed);
    AWARN << "Record queue overflow, dropped "
          << dropped_frames_.load(std::memory_order_relaxed) << " frames ("
          << dropped_bytes_.load(std::memory_order_relaxed) << " bytes) in total.";
  }
}

bool RecordFileWriter::EnqueueMessage(const proto::SingleMessage& message) {
  const int slot_index = slot_pool_.Acquire();
  if (slot_index < 0) {
    HandleDrop(message.content().size());
    return true;
  }
  MessageSlot* slot = slot_pool_.At(slot_index);
  if (slot == nullptr) {
    slot_pool_.Release(slot_index);
    HandleDrop(message.content().size());
    return true;
  }
  if (!slot->Store(message)) {
    slot_pool_.Release(slot_index);
    HandleDrop(message.content().size());
    return true;
  }

  if (!queue_.Push(slot_index)) {
    slot_pool_.Release(slot_index);
    HandleDrop(message.content().size());
    return true;
  }
  {
    std::lock_guard<std::mutex> lock(queue_wait_mutex_);
    queue_wait_cv_.notify_one();
  }
  return true;
}

bool RecordFileWriter::WriteMessage(const proto::SingleMessage& message) {
  if (!is_writing_.load(std::memory_order_acquire) || io_failed_) {
    return false;
  }
  {
    std::lock_guard<std::mutex> channel_lock(channel_map_mutex_);
    auto it = channel_message_number_map_.find(message.channel_name());
    if (it != channel_message_number_map_.end()) {
      it->second++;
    } else {
      channel_message_number_map_.insert(std::make_pair(message.channel_name(), 1));
    }
  }
  return EnqueueMessage(message);
}

bool RecordFileWriter::WriteChunk(Chunk* chunk) {
  const ChunkHeader& chunk_header = chunk->header_;
  uint64_t pos = CurrentPosition();
  if (!WriteSection<ChunkHeader>(chunk_header)) {
    AERROR << "Write chunk header fail";
    return false;
  }
  SingleIndex* single_index = index_.add_indexes();
  single_index->set_type(SectionType::SECTION_CHUNK_HEADER);
  single_index->set_position(pos);
  auto* chunk_header_cache = new ChunkHeaderCache();
  chunk_header_cache->set_begin_time(chunk_header.begin_time());
  chunk_header_cache->set_end_time(chunk_header.end_time());
  chunk_header_cache->set_message_number(chunk_header.message_number());
  chunk_header_cache->set_raw_size(chunk_header.raw_size());
  single_index->set_allocated_chunk_header_cache(chunk_header_cache);

  const int buffer_index = AcquireChunkWriteBufferLocked();
  pos = CurrentPosition();
  const size_t body_size = chunk->body_bytes_.size();
  if (buffer_index >= 0) {
    auto& write_buffer = chunk_buffers_[buffer_index];
    if (body_size <= write_buffer.storage.size() &&
        chunk_buffers_[buffer_index].state == ChunkWriteBuffer::State::FILLING) {
      if (body_size > 0) {
        memcpy(write_buffer.storage.data(), chunk->body_bytes_.data(), body_size);
      }
      write_buffer.used = body_size;
      write_buffer.message_number = chunk_header.message_number();
      write_buffer.header = chunk_header;
      if (!WriteChunkBodyFromFixedBuffer(buffer_index)) {
        AERROR << "Write chunk body from fixed buffer fail";
        write_buffer.state = ChunkWriteBuffer::State::FREE;
        return false;
      }
    } else {
      write_buffer.state = ChunkWriteBuffer::State::FREE;
      if (!WriteSectionRaw(SectionType::SECTION_CHUNK_BODY,
                           std::move(chunk->body_bytes_))) {
        AERROR << "Write chunk body fallback fail";
        return false;
      }
    }
  } else {
    if (!WriteSectionRaw(SectionType::SECTION_CHUNK_BODY,
                         std::move(chunk->body_bytes_))) {
      AERROR << "Write chunk body fallback fail";
      return false;
    }
  }
  header_.set_chunk_number(header_.chunk_number() + 1);
  if (header_.begin_time() == 0 || chunk_header.begin_time() < header_.begin_time()) {
    header_.set_begin_time(chunk_header.begin_time());
  }
  if (chunk_header.end_time() > header_.end_time()) {
    header_.set_end_time(chunk_header.end_time());
  }
  header_.set_message_number(header_.message_number() + chunk_header.message_number());
  single_index = index_.add_indexes();
  single_index->set_type(SectionType::SECTION_CHUNK_BODY);
  single_index->set_position(pos);
  auto* chunk_body_cache = new ChunkBodyCache();
  chunk_body_cache->set_message_number(chunk_header.message_number());
  single_index->set_allocated_chunk_body_cache(chunk_body_cache);
  return true;
}

bool RecordFileWriter::FlushActiveChunk() {
  std::lock_guard<std::mutex> io_lock(io_mutex_);
  return FlushActiveChunkLocked();
}

bool RecordFileWriter::FlushActiveChunkLocked() {
  if (chunk_active_.empty()) {
    return true;
  }
  if (!WriteChunk(&chunk_active_)) {
    return false;
  }
  chunk_active_.clear();
  ResetBatchStateLocked();
  return true;
}

void RecordFileWriter::ResetBatchStateLocked() {
  batch_message_count_ = 0;
  batch_payload_bytes_ = 0;
  batch_open_ = false;
}

void RecordFileWriter::BackendLoop() {
  const auto wait_budget = std::chrono::microseconds(kMaxBatchWaitUs);
  while (true) {
    int slot_index = kInvalidSlot;
    if (!queue_.Pop(&slot_index)) {
      if (!is_writing_.load(std::memory_order_acquire) && queue_.Empty()) {
        break;
      }
      std::unique_lock<std::mutex> wait_lock(queue_wait_mutex_);
      queue_wait_cv_.wait_for(wait_lock, wait_budget, [this] {
        return !queue_.Empty() || !is_writing_.load(std::memory_order_acquire);
      });
      wait_lock.unlock();
      std::lock_guard<std::mutex> io_lock(io_mutex_);
      if (batch_open_ &&
          std::chrono::steady_clock::now() - batch_start_time_ >= wait_budget) {
        if (!FlushActiveChunkLocked()) {
          io_failed_ = true;
        }
      }
      continue;
    }

    MessageSlot* slot = slot_pool_.At(slot_index);
    if (slot == nullptr) {
      continue;
    }

    {
      std::lock_guard<std::mutex> io_lock(io_mutex_);
      const uint64_t message_time = slot->time_ns;
      const uint64_t message_size = slot->payload_size;
      if (!batch_open_) {
        batch_start_time_ = std::chrono::steady_clock::now();
        batch_open_ = true;
      }
      chunk_active_.add(slot->channel_name, slot->time_ns, slot->PayloadData(),
                        slot->payload_size);
      ++batch_message_count_;
      batch_payload_bytes_ += message_size;
      const bool over_interval =
          header_.chunk_interval() > 0 && batch_message_count_ > 0 &&
          message_time - chunk_active_.header_.begin_time() > header_.chunk_interval();
      const bool over_raw_size = header_.chunk_raw_size() > 0 &&
                                 chunk_active_.header_.raw_size() >
                                     header_.chunk_raw_size();
      const bool over_batch_msg = batch_message_count_ >= kMaxBatchMsgs;
      const bool over_batch_bytes = batch_payload_bytes_ >= kMaxBatchBytes;
      if (over_interval || over_raw_size || over_batch_msg || over_batch_bytes) {
        if (!FlushActiveChunkLocked()) {
          io_failed_ = true;
        }
      }
      bool completed = false;
      PollCompletions(false, &completed);
    }
    slot->Reset();
    slot_pool_.Release(slot_index);
  }

  std::lock_guard<std::mutex> io_lock(io_mutex_);
  if (!FlushActiveChunkLocked()) {
    io_failed_ = true;
  }
}

bool RecordFileWriter::WriteIndex() {
  for (int i = 0; i < index_.indexes_size(); i++) {
    SingleIndex* single_index = index_.mutable_indexes(i);
    if (single_index->type() == SectionType::SECTION_CHANNEL) {
      ChannelCache* channel_cache = single_index->mutable_channel_cache();
      std::lock_guard<std::mutex> channel_lock(channel_map_mutex_);
      auto it = channel_message_number_map_.find(channel_cache->name());
      if (it != channel_message_number_map_.end()) {
        channel_cache->set_message_number(it->second);
      }
    }
  }
  header_.set_index_position(CurrentPosition());
  if (!WriteSection<proto::Index>(index_)) {
    AERROR << "Write index section fail";
    return false;
  }
  return true;
}

void RecordFileWriter::Close() {
  if (!is_writing_.load(std::memory_order_acquire)) {
    return;
  }

  is_writing_.store(false, std::memory_order_release);
  queue_wait_cv_.notify_all();
  if (backend_thread_.joinable()) {
    backend_thread_.join();
  }

  {
    std::lock_guard<std::mutex> io_lock(io_mutex_);
    if (!WriteIndex()) {
      AERROR << "Write index section failed, file: " << path_;
    }
    header_.set_is_complete(true);
    if (!SubmitHeaderRewrite(header_)) {
      AERROR << "Overwrite header section failed, file: " << path_;
    }
    if (!FlushCompletions()) {
      AERROR << "Flush io_uring completions failed, file: " << path_;
    }
  }

  if (ring_initialized_) {
    if (ring_fixed_buffers_registered_) {
      io_uring_unregister_buffers(&ring_);
      ring_fixed_buffers_registered_ = false;
    }
    registered_iovecs_.clear();
    if (ring_fixed_file_registered_) {
      io_uring_unregister_files(&ring_);
      ring_fixed_file_registered_ = false;
    }
    io_uring_queue_exit(&ring_);
    ring_initialized_ = false;
  }
  if (fd_ >= 0) {
    if (close(fd_) < 0) {
      AERROR << "Close file failed, file: " << path_ << ", fd: " << fd_
             << ", errno: " << errno;
    }
    fd_ = -1;
  }
  ResetChunkBuffers();
}

uint64_t RecordFileWriter::GetMessageNumber(const std::string& channel_name) const {
  std::lock_guard<std::mutex> channel_lock(channel_map_mutex_);
  auto search = channel_message_number_map_.find(channel_name);
  if (search != channel_message_number_map_.end()) {
    return search->second;
  }
  return 0;
}

}  // namespace record
}  // namespace cyber
}  // namespace apollo
