/******************************************************************************
 * Copyright 2025 WheelOS. All Rights Reserved.
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

#ifndef CYBER_TRANSPORT_MESSAGE_POD_MESSAGE_H_
#define CYBER_TRANSPORT_MESSAGE_POD_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <type_traits>

namespace apollo {
namespace cyber {
namespace transport {

enum class PodPayloadKind : uint32_t {
  UNKNOWN = 0,
  IMAGE = 1,
  POINT_CLOUD = 2,
  TENSOR = 3,
};

struct alignas(64) PodChunkHeader {
  static constexpr uint32_t kMagic = 0x504F444D;  // PODM
  static constexpr uint16_t kVersion = 1;

  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint16_t header_size = sizeof(PodChunkHeader);
  uint32_t payload_kind = static_cast<uint32_t>(PodPayloadKind::UNKNOWN);
  uint64_t timestamp_ns = 0;
  uint64_t frame_id = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride_bytes = 0;
  uint32_t pixel_format = 0;
  uint32_t payload_size = 0;
  uint32_t schema_hash = 0;
  uint64_t reserved[4] = {0, 0, 0, 0};
};

static_assert(std::is_trivially_copyable<PodChunkHeader>::value,
              "PodChunkHeader must stay trivially copyable");

struct PodChunkView {
  PodChunkHeader header;
  const uint8_t* payload = nullptr;
  std::size_t payload_size = 0;
  std::size_t total_size = 0;
};

inline std::size_t PodChunkTotalSize(std::size_t payload_size);
inline const char* PodSchemaDescriptorPrefix();
inline std::string PodSchemaDescriptor();
inline bool IsPodSchemaDescriptor(const std::string& descriptor);
inline PodChunkHeader MakeImagePodChunkHeader(
    uint64_t timestamp_ns, uint64_t frame_id, uint32_t width,
    uint32_t height, uint32_t stride_bytes, uint32_t pixel_format,
    uint32_t payload_size, uint32_t schema_hash = 0);
inline bool BuildPodChunk(const PodChunkHeader& header, const void* payload,
                          std::size_t payload_size, void* dst,
                          std::size_t dst_capacity, std::size_t* dst_size);
inline bool ParsePodChunk(const void* src, std::size_t src_size,
                          PodChunkView* view);

class PodMessage {
 public:
  PodMessage()
      : type_name_(""),
        buffer_(),
        borrowed_buffer_owner_(nullptr),
        data_(nullptr),
        size_(0),
        timestamp_(0) {}

  PodMessage(const PodChunkHeader& header, const void* payload,
             std::size_t payload_size)
      : type_name_(""),
        buffer_(PodChunkTotalSize(payload_size)),
        borrowed_buffer_owner_(nullptr),
        data_(nullptr),
        size_(0),
        timestamp_(header.timestamp_ns) {
    std::size_t written = 0;
    if (BuildPodChunk(header, payload, payload_size, buffer_.data(),
                      buffer_.size(), &written)) {
      RefreshOwnedView(written);
    }
  }

  PodMessage(const PodMessage& other)
      : type_name_(other.type_name_),
        buffer_(),
        borrowed_buffer_owner_(nullptr),
        data_(nullptr),
        size_(0),
        timestamp_(other.timestamp_) {
    AssignFrom(other);
  }

  PodMessage& operator=(const PodMessage& other) {
    if (this != &other) {
      type_name_ = other.type_name_;
      timestamp_ = other.timestamp_;
      AssignFrom(other);
    }
    return *this;
  }
  ~PodMessage() = default;

  class Descriptor {
   public:
    std::string full_name() const { return "apollo.cyber.transport.PodMessage"; }
    std::string name() const { return "apollo.cyber.transport.PodMessage"; }
  };

  static const Descriptor* descriptor() {
    static Descriptor desc;
    return &desc;
  }

  static std::string TypeName() { return "apollo.cyber.transport.PodMessage"; }

  static void GetDescriptorString(const std::string& type,
                                  std::string* desc_str) {
    (void)type;
    if (desc_str != nullptr) {
      *desc_str = PodSchemaDescriptor();
    }
  }

  bool SerializeToArray(void* data, int size) const {
    if (data == nullptr || size < ByteSize()) {
      return false;
    }
    if (size_ == 0) {
      return true;
    }
    std::memcpy(data, data_, size_);
    return true;
  }

  bool SerializeToString(std::string* str) const {
    if (str == nullptr) {
      return false;
    }
    if (size_ == 0) {
      str->clear();
      return true;
    }
    str->assign(reinterpret_cast<const char*>(data_), size_);
    return true;
  }

  bool ParseFromArray(const void* data, int size) {
    if (data == nullptr || size <= 0) {
      ClearView();
      return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    PodChunkView view;
    if (!ParsePodChunk(bytes, static_cast<std::size_t>(size), &view)) {
      ClearView();
      return false;
    }
    borrowed_buffer_owner_.reset();
    buffer_.assign(bytes, bytes + size);
    RefreshOwnedView();
    timestamp_ = view.header.timestamp_ns;
    return true;
  }

  bool ParseFromString(const std::string& str) {
    PodChunkView view;
    if (!ParsePodChunk(str.data(), str.size(), &view)) {
      ClearView();
      return false;
    }
    borrowed_buffer_owner_.reset();
    buffer_.assign(str.begin(), str.end());
    RefreshOwnedView();
    timestamp_ = view.header.timestamp_ns;
    return true;
  }

  bool BorrowFromArray(const void* data, std::size_t size,
                       std::shared_ptr<void> owner = nullptr) {
    if (data == nullptr || size == 0) {
      ClearView();
      return false;
    }
    PodChunkView view;
    if (!ParsePodChunk(data, size, &view)) {
      ClearView();
      return false;
    }
    buffer_.clear();
    borrowed_buffer_owner_ = std::move(owner);
    data_ = static_cast<const uint8_t*>(data);
    size_ = size;
    timestamp_ = view.header.timestamp_ns;
    return true;
  }

  int ByteSize() const { return static_cast<int>(size_); }
  std::size_t ByteSizeLong() const { return size_; }

  const uint8_t* data() const { return data_; }
  std::size_t size() const { return size_; }

  const PodChunkHeader* header() const {
    if (data_ == nullptr || size_ < sizeof(PodChunkHeader)) {
      return nullptr;
    }
    return reinterpret_cast<const PodChunkHeader*>(data_);
  }

  const PodChunkView View() const {
    PodChunkView view;
    ParsePodChunk(data_, size_, &view);
    return view;
  }

  const std::string& type_name() const { return type_name_; }
  void set_type_name(const std::string& name) { type_name_ = name; }
  void SetTypeName(const std::string& name) { type_name_ = name; }

  uint64_t timestamp() const { return timestamp_; }
  void set_timestamp(uint64_t ts) { timestamp_ = ts; }

 private:
  void AssignFrom(const PodMessage& other) {
    ClearView();

    if (other.size_ == 0 || other.data_ == nullptr) {
      return;
    }

    if (!other.buffer_.empty()) {
      buffer_ = other.buffer_;
      RefreshOwnedView(other.size_);
      return;
    }

    const auto* src = other.data_;
    buffer_.assign(src, src + other.size_);
    RefreshOwnedView();
  }

  void ClearView() {
    borrowed_buffer_owner_.reset();
    buffer_.clear();
    data_ = nullptr;
    size_ = 0;
    timestamp_ = 0;
  }

  void RefreshOwnedView() {
    data_ = buffer_.empty() ? nullptr : buffer_.data();
    size_ = buffer_.size();
  }

  void RefreshOwnedView(std::size_t size) {
    data_ = buffer_.empty() ? nullptr : buffer_.data();
    size_ = size;
  }

  std::string type_name_;
  std::vector<uint8_t> buffer_;
  std::shared_ptr<void> borrowed_buffer_owner_;
  const uint8_t* data_;
  std::size_t size_;
  uint64_t timestamp_;
};

inline std::size_t PodChunkTotalSize(std::size_t payload_size) {
  return sizeof(PodChunkHeader) + payload_size;
}

inline const char* PodSchemaDescriptorPrefix() {
  return "wheelos.cyber.pod_schema/v1";
}

inline std::string PodSchemaDescriptor() {
  return std::string(PodSchemaDescriptorPrefix()) +
         "\nmessage_type=apollo.cyber.transport.PodMessage"
         "\nlayout=PodChunkHeader|payload"
         "\nmagic=0x504F444D"
         "\nversion=1"
         "\nheader_size=" + std::to_string(sizeof(PodChunkHeader)) +
         "\nalignment=64"
         "\nendianness=little"
         "\npayload_kinds=UNKNOWN:0,IMAGE:1,POINT_CLOUD:2,TENSOR:3"
         "\nfields=magic:uint32,version:uint16,header_size:uint16,"
         "payload_kind:uint32,timestamp_ns:uint64,frame_id:uint64,"
         "width:uint32,height:uint32,stride_bytes:uint32,pixel_format:uint32,"
         "payload_size:uint32,schema_hash:uint32,reserved:uint64[4]";
}

inline bool IsPodSchemaDescriptor(const std::string& descriptor) {
  const std::string prefix = PodSchemaDescriptorPrefix();
  return descriptor.compare(0, prefix.size(), prefix) == 0;
}

inline PodChunkHeader MakeImagePodChunkHeader(
    uint64_t timestamp_ns, uint64_t frame_id, uint32_t width,
    uint32_t height, uint32_t stride_bytes, uint32_t pixel_format,
    uint32_t payload_size, uint32_t schema_hash) {
  PodChunkHeader header;
  header.timestamp_ns = timestamp_ns;
  header.frame_id = frame_id;
  header.payload_kind = static_cast<uint32_t>(PodPayloadKind::IMAGE);
  header.width = width;
  header.height = height;
  header.stride_bytes = stride_bytes;
  header.pixel_format = pixel_format;
  header.payload_size = payload_size;
  header.schema_hash = schema_hash;
  return header;
}

inline bool BuildPodChunk(const PodChunkHeader& header, const void* payload,
                          std::size_t payload_size, void* dst,
                          std::size_t dst_capacity, std::size_t* dst_size) {
  if (dst == nullptr || dst_size == nullptr) {
    return false;
  }
  if (payload_size > 0 && payload == nullptr) {
    return false;
  }
  if (header.magic != PodChunkHeader::kMagic ||
      header.version != PodChunkHeader::kVersion ||
      header.header_size != sizeof(PodChunkHeader) ||
      header.payload_size != payload_size) {
    return false;
  }

  const std::size_t total_size = PodChunkTotalSize(payload_size);
  if (dst_capacity < total_size) {
    return false;
  }

  auto* out = static_cast<uint8_t*>(dst);
  PodChunkHeader wire_header{};
  wire_header.magic = header.magic;
  wire_header.version = header.version;
  wire_header.header_size = header.header_size;
  wire_header.payload_kind = header.payload_kind;
  wire_header.timestamp_ns = header.timestamp_ns;
  wire_header.frame_id = header.frame_id;
  wire_header.width = header.width;
  wire_header.height = header.height;
  wire_header.stride_bytes = header.stride_bytes;
  wire_header.pixel_format = header.pixel_format;
  wire_header.payload_size = header.payload_size;
  wire_header.schema_hash = header.schema_hash;
  for (std::size_t i = 0;
       i < sizeof(wire_header.reserved) / sizeof(wire_header.reserved[0]);
       ++i) {
    wire_header.reserved[i] = header.reserved[i];
  }
  std::memcpy(out, &wire_header, sizeof(wire_header));
  if (payload_size > 0) {
    std::memcpy(out + sizeof(header), payload, payload_size);
  }
  *dst_size = total_size;
  return true;
}

inline bool ParsePodChunk(const void* src, std::size_t src_size,
                          PodChunkView* view) {
  if (src == nullptr || view == nullptr ||
      src_size < sizeof(PodChunkHeader)) {
    return false;
  }

  PodChunkHeader header;
  std::memcpy(&header, src, sizeof(header));
  if (header.magic != PodChunkHeader::kMagic ||
      header.version != PodChunkHeader::kVersion ||
      header.header_size != sizeof(PodChunkHeader)) {
    return false;
  }

  const std::size_t total_size = PodChunkTotalSize(header.payload_size);
  if (src_size < total_size) {
    return false;
  }

  view->header = header;
  view->payload = static_cast<const uint8_t*>(src) + sizeof(PodChunkHeader);
  view->payload_size = header.payload_size;
  view->total_size = total_size;
  return true;
}

template <typename T>
inline const T* PayloadAs(const PodChunkView& view) {
  if (view.payload == nullptr || view.payload_size < sizeof(T)) {
    return nullptr;
  }
  if (reinterpret_cast<std::uintptr_t>(view.payload) % alignof(T) != 0) {
    return nullptr;
  }
  return reinterpret_cast<const T*>(view.payload);
}

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_TRANSPORT_MESSAGE_POD_MESSAGE_H_
