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

#include "cyber/transport/message/pod_message.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"

namespace apollo {
namespace cyber {
namespace transport {

TEST(PodMessageTest, build_and_parse_image_chunk) {
  const std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 0, 7, 8};
  const auto header = MakeImagePodChunkHeader(
      /*timestamp_ns=*/123456789, /*frame_id=*/42, /*width=*/1920,
      /*height=*/1080, /*stride_bytes=*/5760, /*pixel_format=*/24,
      static_cast<uint32_t>(payload.size()), /*schema_hash=*/0xabcdef01);

  std::vector<uint8_t> chunk(PodChunkTotalSize(payload.size()));
  std::size_t written = 0;
  ASSERT_TRUE(BuildPodChunk(header, payload.data(), payload.size(),
                            chunk.data(), chunk.size(), &written));
  ASSERT_EQ(written, chunk.size());

  PodChunkView view;
  ASSERT_TRUE(ParsePodChunk(chunk.data(), chunk.size(), &view));
  EXPECT_EQ(view.header.magic, PodChunkHeader::kMagic);
  EXPECT_EQ(view.header.version, PodChunkHeader::kVersion);
  EXPECT_EQ(view.header.payload_kind,
            static_cast<uint32_t>(PodPayloadKind::IMAGE));
  EXPECT_EQ(view.header.timestamp_ns, 123456789u);
  EXPECT_EQ(view.header.frame_id, 42u);
  EXPECT_EQ(view.header.width, 1920u);
  EXPECT_EQ(view.header.height, 1080u);
  EXPECT_EQ(view.header.stride_bytes, 5760u);
  EXPECT_EQ(view.header.pixel_format, 24u);
  EXPECT_EQ(view.header.payload_size, payload.size());
  EXPECT_EQ(view.header.schema_hash, 0xabcdef01u);
  EXPECT_EQ(view.payload_size, payload.size());
  EXPECT_EQ(view.total_size, chunk.size());
  ASSERT_NE(view.payload, nullptr);
  EXPECT_EQ(0, std::memcmp(view.payload, payload.data(), payload.size()));
}

TEST(PodMessageTest, parse_rejects_invalid_magic) {
  std::vector<uint8_t> chunk(sizeof(PodChunkHeader), 0);
  PodChunkHeader header;
  header.magic = 0;
  std::memcpy(chunk.data(), &header, sizeof(header));

  PodChunkView view;
  EXPECT_FALSE(ParsePodChunk(chunk.data(), chunk.size(), &view));
}

TEST(PodMessageTest, build_rejects_payload_mismatch) {
  const std::vector<uint8_t> payload = {1, 2, 3};
  auto header = MakeImagePodChunkHeader(
      /*timestamp_ns=*/1, /*frame_id=*/2, /*width=*/3, /*height=*/4,
      /*stride_bytes=*/5, /*pixel_format=*/6, /*payload_size=*/4);

  std::vector<uint8_t> chunk(PodChunkTotalSize(payload.size()));
  std::size_t written = 0;
  EXPECT_FALSE(BuildPodChunk(header, payload.data(), payload.size(),
                             chunk.data(), chunk.size(), &written));
}

TEST(PodMessageTest, payload_as_returns_null_for_small_view) {
  PodChunkView view;
  view.payload = nullptr;
  view.payload_size = 0;
  EXPECT_EQ(PayloadAs<uint64_t>(view), nullptr);
}

}  // namespace transport
}  // namespace cyber
}  // namespace apollo
