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

#ifndef CYBER_TRANSPORT_ICEORYX_CHUNK_H_
#define CYBER_TRANSPORT_ICEORYX_CHUNK_H_

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>

#include "cyber/message/message_traits.h"
#include "cyber/transport/message/pod_message.h"

namespace apollo {
namespace cyber {
namespace transport {

constexpr std::size_t kIceoryxChunkPayloadCapacity =
    (8u * 1024u * 1024u) - 256u;

struct alignas(8) IceoryxByteChunk {
  uint32_t payload_size = 0;
  uint32_t reserved = 0;
  std::array<uint8_t, kIceoryxChunkPayloadCapacity> payload{};
};

static_assert(std::is_trivially_copyable<IceoryxByteChunk>::value,
              "IceoryxByteChunk must stay trivially copyable");

template <typename M>
inline bool EncodeMessageToIceoryxChunk(const M& message,
                                        IceoryxByteChunk* chunk) {
  if (chunk == nullptr) {
    return false;
  }
  std::string serialized;
  if (!message::SerializeToString(message, &serialized)) {
    return false;
  }
  if (serialized.size() > chunk->payload.size()) {
    return false;
  }
  chunk->payload_size = static_cast<uint32_t>(serialized.size());
  if (!serialized.empty()) {
    std::memcpy(chunk->payload.data(), serialized.data(), serialized.size());
  }
  return true;
}

inline bool EncodeMessageToIceoryxChunk(const PodMessage& message,
                                        IceoryxByteChunk* chunk) {
  if (chunk == nullptr || message.data() == nullptr ||
      message.size() > chunk->payload.size()) {
    return false;
  }
  chunk->payload_size = static_cast<uint32_t>(message.size());
  if (message.size() > 0) {
    std::memcpy(chunk->payload.data(), message.data(), message.size());
  }
  return true;
}

template <typename M>
inline bool DecodeMessageFromIceoryxChunk(const IceoryxByteChunk& chunk,
                                          M* message) {
  if (message == nullptr) {
    return false;
  }
  if (chunk.payload_size > chunk.payload.size()) {
    return false;
  }
  std::string serialized(
      reinterpret_cast<const char*>(chunk.payload.data()), chunk.payload_size);
  return message::ParseFromString(serialized, message);
}

template <typename M>
inline bool DecodeMessageFromIceoryxChunk(const IceoryxByteChunk& chunk,
                                          std::shared_ptr<void> owner,
                                          M* message) {
  (void)owner;
  return DecodeMessageFromIceoryxChunk(chunk, message);
}

inline bool DecodeMessageFromIceoryxChunk(const IceoryxByteChunk& chunk,
                                          std::shared_ptr<void> owner,
                                          PodMessage* message) {
  if (message == nullptr || chunk.payload_size > chunk.payload.size()) {
    return false;
  }
  return message->BorrowFromArray(chunk.payload.data(), chunk.payload_size,
                                  std::move(owner));
}

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_TRANSPORT_ICEORYX_CHUNK_H_
