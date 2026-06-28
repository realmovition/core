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

#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#include "cyber/common/file.h"

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
                                    const std::string& channel_name,
                                    uint64_t time_ns, const char* payload,
                                    size_t payload_size) {
  const size_t channel_size = channel_name.size();
  const size_t single_message_size =
      1 + VarintSize(channel_size) + channel_size + 1 + VarintSize(time_ns) +
      1 + VarintSize(payload_size) + payload_size;
  const size_t chunk_entry_size =
      1 + VarintSize(single_message_size) + single_message_size;
  const size_t old_size = body_bytes->size();
  body_bytes->resize(old_size + chunk_entry_size);
  char* out = body_bytes->data() + old_size;

  *out++ = static_cast<char>(0x0A);
  out += EncodeVarint(out, static_cast<uint64_t>(single_message_size));

  *out++ = static_cast<char>(0x0A);
  out += EncodeVarint(out, static_cast<uint64_t>(channel_size));
  if (channel_size > 0) {
    memcpy(out, channel_name.data(), channel_size);
    out += channel_size;
  }

  *out++ = static_cast<char>(0x10);
  out += EncodeVarint(out, time_ns);

  *out++ = static_cast<char>(0x1A);
  out += EncodeVarint(out, static_cast<uint64_t>(payload_size));
  if (payload_size > 0) {
    memcpy(out, payload, payload_size);
  }
}

uint64_t ParseUint64Env(const char* name, uint64_t fallback) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || raw[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long long parsed = std::strtoull(raw, &end, 10);
  if (errno != 0 || end == raw || (end != nullptr && *end != '\0')) {
    AWARN << "Invalid value for env " << name << ": " << raw
          << ", fallback to " << fallback;
    return fallback;
  }
  return static_cast<uint64_t>(parsed);
}

uint64_t RoundUpTo(uint64_t value, uint64_t step) {
  if (step == 0) {
    return value;
  }
  const uint64_t rem = value % step;
  if (rem == 0) {
    return value;
  }
  const uint64_t add = step - rem;
  if (value > std::numeric_limits<uint64_t>::max() - add) {
    return std::numeric_limits<uint64_t>::max();
  }
  return value + add;
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

void Chunk::add(const std::string& channel_name, uint64_t time_ns,
                const char* payload, size_t payload_size) {
  AppendSingleMessageToChunkBody(&body_bytes_, channel_name, time_ns, payload,
                                 payload_size);
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

RecordFileWriter::RecordFileWriter() = default;

RecordFileWriter::~RecordFileWriter() { Close(); }

void RecordFileWriter::LoadRuntimeTuningFromEnv() {
  prealloc_step_bytes_ = ParseUint64Env(
      "WHEELOS_RECORD_WRITER_PREALLOC_STEP_BYTES", kDefaultPreallocStepBytes);
  fdatasync_interval_bytes_ = ParseUint64Env(
      "WHEELOS_RECORD_WRITER_FDATASYNC_INTERVAL_BYTES", 0);
}

bool RecordFileWriter::EnsureFilePreallocatedLocked(uint64_t required_size_bytes) {
  if (prealloc_step_bytes_ == 0 || !prealloc_supported_) {
    return true;
  }
  if (required_size_bytes <= preallocated_size_bytes_) {
    return true;
  }
  const uint64_t target_size = RoundUpTo(required_size_bytes, prealloc_step_bytes_);
  if (target_size < required_size_bytes) {
    AERROR << "File preallocation size overflow, required: " << required_size_bytes
           << ", step: " << prealloc_step_bytes_;
    return false;
  }
  const uint64_t alloc_len = target_size - preallocated_size_bytes_;
  int rc = 0;
  do {
    rc = posix_fallocate(fd_, static_cast<off_t>(preallocated_size_bytes_),
                         static_cast<off_t>(alloc_len));
  } while (rc == EINTR);
  if (rc == 0) {
    preallocated_size_bytes_ = target_size;
    return true;
  }
  if (rc == EOPNOTSUPP || rc == ENOTSUP || rc == ENOSYS || rc == EINVAL) {
    prealloc_supported_ = false;
    if (!prealloc_warned_) {
      AWARN << "posix_fallocate unsupported for file " << path_
            << ", fallback to sparse growth.";
      prealloc_warned_ = true;
    }
    return true;
  }
  AERROR << "posix_fallocate failed, file: " << path_ << ", rc: " << rc;
  return false;
}

bool RecordFileWriter::MaybeDataSyncLocked(bool force) {
  if (fdatasync_interval_bytes_ == 0) {
    return true;
  }
  if (unsynced_bytes_ == 0) {
    return true;
  }
  if (!force && unsynced_bytes_ < fdatasync_interval_bytes_) {
    return true;
  }
  while (fdatasync(fd_) < 0) {
    if (errno == EINTR) {
      continue;
    }
    AERROR << "fdatasync failed, file: " << path_ << ", errno: " << errno;
    return false;
  }
  unsynced_bytes_ = 0;
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

  LoadRuntimeTuningFromEnv();
  preallocated_size_bytes_ = 0;
  unsynced_bytes_ = 0;
  prealloc_supported_ = true;
  prealloc_warned_ = false;

  header_.Clear();
  index_.Clear();
  logical_position_ = 0;
  chunk_interval_ns_ = 0;
  chunk_raw_size_bytes_ = 0;
  io_failed_.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> channel_lock(channel_map_mutex_);
    channel_message_number_map_.clear();
  }
  {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    active_chunk_.reset(new (std::nothrow) Chunk());
    flush_chunk_.reset(new (std::nothrow) Chunk());
    if (active_chunk_ == nullptr || flush_chunk_ == nullptr) {
      AERROR << "Allocate chunk buffers failed.";
      close(fd_);
      fd_ = -1;
      active_chunk_.reset();
      flush_chunk_.reset();
      return false;
    }
    active_chunk_->body_bytes_.reserve(kInitialBodyReserveBytes);
    flush_chunk_->body_bytes_.reserve(kInitialBodyReserveBytes);
    flush_pending_ = false;
  }

  is_writing_.store(true, std::memory_order_release);
  backend_thread_ = std::thread([this]() { BackendLoop(); });
  return true;
}

bool RecordFileWriter::WriteBuffers(const struct iovec* iov, int iovcnt,
                                    size_t expected, uint64_t offset) {
  if (!EnsureFilePreallocatedLocked(offset + expected)) {
    return false;
  }
  std::vector<struct iovec> pending(iov, iov + iovcnt);
  size_t written_total = 0;
  while (written_total < expected) {
    const ssize_t written =
        pwritev(fd_, pending.data(), static_cast<int>(pending.size()),
                static_cast<off_t>(offset + written_total));
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      AERROR << "pwritev failed, file: " << path_ << ", fd: " << fd_
             << ", errno: " << errno;
      return false;
    }
    if (written == 0) {
      AERROR << "pwritev wrote zero bytes, file: " << path_ << ", fd: " << fd_
             << ", expect: " << expected;
      return false;
    }

    written_total += static_cast<size_t>(written);
    size_t advance = static_cast<size_t>(written);
    while (advance > 0 && !pending.empty()) {
      if (advance >= pending.front().iov_len) {
        advance -= pending.front().iov_len;
        pending.erase(pending.begin());
      } else {
        pending.front().iov_base =
            static_cast<char*>(pending.front().iov_base) + advance;
        pending.front().iov_len -= advance;
        advance = 0;
      }
    }
  }
  unsynced_bytes_ += expected;
  return MaybeDataSyncLocked(false);
}

bool RecordFileWriter::WriteHeaderRewrite(const proto::Header& header) {
  const size_t message_size = header.ByteSizeLong();
  if (message_size > HEADER_LENGTH) {
    AERROR << "Header size exceeds reserved length, size: " << message_size
           << ", max: " << HEADER_LENGTH;
    return false;
  }

  Section section = {proto::SectionType::SECTION_HEADER,
                     static_cast<int64_t>(message_size)};
  std::vector<char> payload(message_size);
  if (!header.SerializeToArray(payload.data(), static_cast<int>(payload.size()))) {
    AERROR << "Serialize header failed.";
    return false;
  }
  std::vector<char> padding(HEADER_LENGTH - message_size, '0');
  std::array<struct iovec, 3> iov = {};
  iov[0].iov_base = &section;
  iov[0].iov_len = sizeof(Section);
  iov[1].iov_base = payload.data();
  iov[1].iov_len = payload.size();
  iov[2].iov_base = padding.data();
  iov[2].iov_len = padding.size();
  return WriteBuffers(iov.data(), static_cast<int>(iov.size()),
                      sizeof(Section) + HEADER_LENGTH, 0);
}

bool RecordFileWriter::WriteHeader(const Header& header) {
  std::lock_guard<std::mutex> io_lock(io_mutex_);
  header_ = header;
  chunk_interval_ns_ = header.chunk_interval();
  chunk_raw_size_bytes_ = header.chunk_raw_size();
  if (!WriteHeaderRewrite(header_)) {
    return false;
  }
  if (logical_position_ == 0) {
    logical_position_ = sizeof(Section) + HEADER_LENGTH;
    header_.set_size(logical_position_);
  }
  return true;
}

bool RecordFileWriter::WriteSectionRaw(proto::SectionType type,
                                       std::vector<char>&& payload) {
  const uint64_t payload_size = static_cast<uint64_t>(payload.size());
  Section section = {type, static_cast<int64_t>(payload_size)};
  std::array<struct iovec, 2> iov = {};
  iov[0].iov_base = &section;
  iov[0].iov_len = sizeof(Section);
  iov[1].iov_base = payload.data();
  iov[1].iov_len = payload.size();
  if (!WriteBuffers(iov.data(), static_cast<int>(iov.size()),
                    sizeof(Section) + payload.size(), logical_position_)) {
    return false;
  }
  logical_position_ += sizeof(Section) + payload_size;
  header_.set_size(logical_position_);
  return true;
}

bool RecordFileWriter::WriteChannel(const Channel& channel) {
  std::lock_guard<std::mutex> io_lock(io_mutex_);
  const uint64_t pos = CurrentPosition();
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

bool RecordFileWriter::RotateActiveChunkLocked(
    std::unique_lock<std::mutex>* state_lock) {
  if (active_chunk_ == nullptr || active_chunk_->empty()) {
    return true;
  }
  while (flush_pending_ && !io_failed_.load(std::memory_order_acquire)) {
    flush_slot_cv_.wait(*state_lock);
  }
  if (io_failed_.load(std::memory_order_acquire) || flush_chunk_ == nullptr) {
    return false;
  }
  std::swap(active_chunk_, flush_chunk_);
  active_chunk_->clear();
  flush_pending_ = true;
  backend_cv_.notify_one();
  return true;
}

bool RecordFileWriter::WriteMessage(const proto::SingleMessage& message) {
  if (!is_writing_.load(std::memory_order_acquire) ||
      io_failed_.load(std::memory_order_acquire)) {
    return false;
  }

  {
    std::unique_lock<std::mutex> state_lock(state_mutex_);
    if (active_chunk_ == nullptr) {
      return false;
    }
    active_chunk_->add(message);
    const bool over_interval =
        chunk_interval_ns_ > 0 &&
        message.time() - active_chunk_->header_.begin_time() > chunk_interval_ns_;
    const bool over_raw_size =
        chunk_raw_size_bytes_ > 0 &&
        active_chunk_->header_.raw_size() > chunk_raw_size_bytes_;
    if ((over_interval || over_raw_size) &&
        !RotateActiveChunkLocked(&state_lock)) {
      return false;
    }
  }

  {
    std::lock_guard<std::mutex> channel_lock(channel_map_mutex_);
    auto it = channel_message_number_map_.find(message.channel_name());
    if (it != channel_message_number_map_.end()) {
      it->second++;
    } else {
      channel_message_number_map_.insert(
          std::make_pair(message.channel_name(), 1));
    }
  }
  return !io_failed_.load(std::memory_order_acquire);
}

bool RecordFileWriter::WriteChunk(Chunk* chunk) {
  if (chunk == nullptr || chunk->empty()) {
    return true;
  }

  const ChunkHeader& chunk_header = chunk->header_;
  std::vector<char> chunk_header_payload(chunk_header.ByteSizeLong());
  if (!chunk_header.SerializeToArray(
          chunk_header_payload.data(),
          static_cast<int>(chunk_header_payload.size()))) {
    AERROR << "Serialize chunk header failed.";
    return false;
  }

  const uint64_t chunk_header_pos = CurrentPosition();
  const uint64_t chunk_body_pos =
      chunk_header_pos + sizeof(Section) + chunk_header_payload.size();
  Section chunk_header_section = {
      proto::SectionType::SECTION_CHUNK_HEADER,
      static_cast<int64_t>(chunk_header_payload.size())};
  Section chunk_body_section = {proto::SectionType::SECTION_CHUNK_BODY,
                                static_cast<int64_t>(chunk->body_bytes_.size())};
  std::array<struct iovec, 4> iov = {};
  iov[0].iov_base = &chunk_header_section;
  iov[0].iov_len = sizeof(Section);
  iov[1].iov_base = chunk_header_payload.data();
  iov[1].iov_len = chunk_header_payload.size();
  iov[2].iov_base = &chunk_body_section;
  iov[2].iov_len = sizeof(Section);
  iov[3].iov_base = chunk->body_bytes_.data();
  iov[3].iov_len = chunk->body_bytes_.size();
  const size_t total_bytes = sizeof(Section) + chunk_header_payload.size() +
                             sizeof(Section) + chunk->body_bytes_.size();
  if (!WriteBuffers(iov.data(), static_cast<int>(iov.size()), total_bytes,
                    chunk_header_pos)) {
    return false;
  }

  logical_position_ += total_bytes;
  header_.set_size(logical_position_);
  header_.set_chunk_number(header_.chunk_number() + 1);
  if (header_.begin_time() == 0 ||
      chunk_header.begin_time() < header_.begin_time()) {
    header_.set_begin_time(chunk_header.begin_time());
  }
  if (chunk_header.end_time() > header_.end_time()) {
    header_.set_end_time(chunk_header.end_time());
  }
  header_.set_message_number(header_.message_number() +
                             chunk_header.message_number());

  SingleIndex* single_index = index_.add_indexes();
  single_index->set_type(SectionType::SECTION_CHUNK_HEADER);
  single_index->set_position(chunk_header_pos);
  auto* chunk_header_cache = new ChunkHeaderCache();
  chunk_header_cache->set_begin_time(chunk_header.begin_time());
  chunk_header_cache->set_end_time(chunk_header.end_time());
  chunk_header_cache->set_message_number(chunk_header.message_number());
  chunk_header_cache->set_raw_size(chunk_header.raw_size());
  single_index->set_allocated_chunk_header_cache(chunk_header_cache);

  single_index = index_.add_indexes();
  single_index->set_type(SectionType::SECTION_CHUNK_BODY);
  single_index->set_position(chunk_body_pos);
  auto* chunk_body_cache = new ChunkBodyCache();
  chunk_body_cache->set_message_number(chunk_header.message_number());
  single_index->set_allocated_chunk_body_cache(chunk_body_cache);
  return true;
}

void RecordFileWriter::BackendLoop() {
  Chunk local_chunk;
  local_chunk.body_bytes_.reserve(kInitialBodyReserveBytes);
  while (true) {
    {
      std::unique_lock<std::mutex> state_lock(state_mutex_);
      backend_cv_.wait(state_lock, [this] {
        return flush_pending_ || !is_writing_.load(std::memory_order_acquire);
      });
      if (!flush_pending_ && !is_writing_.load(std::memory_order_acquire)) {
        break;
      }
      if (flush_chunk_ == nullptr) {
        io_failed_.store(true, std::memory_order_release);
        is_writing_.store(false, std::memory_order_release);
        flush_slot_cv_.notify_all();
        backend_cv_.notify_all();
        break;
      }
      std::swap(local_chunk, *flush_chunk_);
      flush_pending_ = false;
      flush_slot_cv_.notify_all();
    }

    {
      std::lock_guard<std::mutex> io_lock(io_mutex_);
      if (!WriteChunk(&local_chunk)) {
        io_failed_.store(true, std::memory_order_release);
      }
    }
    if (io_failed_.load(std::memory_order_acquire)) {
      is_writing_.store(false, std::memory_order_release);
      flush_slot_cv_.notify_all();
      backend_cv_.notify_all();
      break;
    }
    local_chunk.clear();
    if (local_chunk.body_bytes_.capacity() < kInitialBodyReserveBytes) {
      local_chunk.body_bytes_.reserve(kInitialBodyReserveBytes);
    }
  }
}

bool RecordFileWriter::WriteIndex() {
  {
    std::lock_guard<std::mutex> channel_lock(channel_map_mutex_);
    for (int i = 0; i < index_.indexes_size(); ++i) {
      SingleIndex* single_index = index_.mutable_indexes(i);
      if (single_index->type() != SectionType::SECTION_CHANNEL) {
        continue;
      }
      ChannelCache* channel_cache = single_index->mutable_channel_cache();
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
  const bool was_writing =
      is_writing_.exchange(false, std::memory_order_acq_rel);
  if (!was_writing && fd_ < 0) {
    return;
  }

  {
    std::unique_lock<std::mutex> state_lock(state_mutex_);
    if (!io_failed_.load(std::memory_order_acquire) && active_chunk_ != nullptr &&
        !active_chunk_->empty()) {
      while (flush_pending_ && !io_failed_.load(std::memory_order_acquire)) {
        flush_slot_cv_.wait(state_lock);
      }
      if (!io_failed_.load(std::memory_order_acquire) &&
          flush_chunk_ != nullptr) {
        std::swap(active_chunk_, flush_chunk_);
        active_chunk_->clear();
        flush_pending_ = true;
      }
    }
  }
  backend_cv_.notify_one();
  if (backend_thread_.joinable()) {
    backend_thread_.join();
  }

  {
    std::lock_guard<std::mutex> io_lock(io_mutex_);
    if (!io_failed_.load(std::memory_order_acquire)) {
      if (!WriteIndex()) {
        AERROR << "Write index section failed, file: " << path_;
        io_failed_.store(true, std::memory_order_release);
      }
    }
    if (!io_failed_.load(std::memory_order_acquire)) {
      header_.set_is_complete(true);
      if (!WriteHeaderRewrite(header_)) {
        AERROR << "Overwrite header section failed, file: " << path_;
        io_failed_.store(true, std::memory_order_release);
      }
    }
    if (!io_failed_.load(std::memory_order_acquire)) {
      if (ftruncate(fd_, static_cast<off_t>(logical_position_)) < 0) {
        AERROR << "ftruncate failed, file: " << path_
               << ", logical size: " << logical_position_
               << ", errno: " << errno;
        io_failed_.store(true, std::memory_order_release);
      } else {
        preallocated_size_bytes_ = logical_position_;
      }
    }
    if (!io_failed_.load(std::memory_order_acquire) &&
        !MaybeDataSyncLocked(true)) {
      io_failed_.store(true, std::memory_order_release);
    }
  }

  if (fd_ >= 0) {
    if (close(fd_) < 0) {
      AERROR << "Close file failed, file: " << path_ << ", fd: " << fd_
             << ", errno: " << errno;
    }
    fd_ = -1;
  }

  {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    active_chunk_.reset();
    flush_chunk_.reset();
    flush_pending_ = false;
  }
}

uint64_t RecordFileWriter::GetMessageNumber(
    const std::string& channel_name) const {
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
