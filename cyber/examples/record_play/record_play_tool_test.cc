// Copyright 2026 WheelOS. All Rights Reserved.
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

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "cyber/cyber.h"
#include "cyber/examples/record_play/record_play_tool.h"
#include "cyber/message/raw_message.h"

namespace apollo {
namespace cyber {
namespace examples {
namespace record_play {
namespace {

constexpr char kTestSource[] = "record_play_tool_test_source.record";
constexpr char kTestConverted[] = "record_play_tool_test_converted.record";
constexpr char kTestManifest[] = "record_play_tool_test.manifest";

void WriteSourceRecord() {
  record::RecordWriter writer;
  writer.SetSizeOfFileSegmentation(0);
  writer.SetIntervalOfFileSegmentation(0);
  ASSERT_TRUE(writer.Open(kTestSource));

  const std::vector<std::string> channels = {
      kImageFront6mm, kImageFront12mm, kPointCloud64};
  for (const auto& channel : channels) {
    for (int i = 0; i < 3; ++i) {
      auto msg = std::make_shared<message::RawMessage>(
          channel + "_" + std::to_string(i));
      ASSERT_TRUE(writer.WriteMessage(channel, msg, 1000 + i));
    }
  }
  writer.Close();
}

}  // namespace

TEST(RecordPlayToolTest, ConvertAndBorrowRoundTrip) {
  WriteSourceRecord();

  ConvertedRecordResult result;
  ASSERT_TRUE(ConvertRecordToPod(kTestSource, kTestConverted, kTestManifest,
                                 /*max_per_channel=*/8, &result));
  EXPECT_EQ(result.channels, 3U);
  EXPECT_EQ(result.messages, 9U);
  EXPECT_GT(result.bytes, 0U);

  record::RecordReader converted_reader(kTestConverted);
  ASSERT_TRUE(converted_reader.IsValid());
  for (const auto& channel : converted_reader.GetChannelList()) {
    EXPECT_EQ(converted_reader.GetMessageType(channel),
              transport::PodMessage::TypeName());
    EXPECT_TRUE(transport::IsPodSchemaDescriptor(
        converted_reader.GetProtoDesc(channel)));
  }

  auto pod_stats = BenchmarkPodBorrow(kTestConverted);
  EXPECT_EQ(pod_stats.messages, 9U);
  EXPECT_GT(pod_stats.bytes, 0U);

  ASSERT_TRUE(std::filesystem::exists(kTestManifest));
  ASSERT_TRUE(std::filesystem::exists(kTestConverted));

  ASSERT_TRUE(std::remove(kTestSource) == 0);
  ASSERT_TRUE(std::remove(kTestConverted) == 0);
  ASSERT_TRUE(std::remove(kTestManifest) == 0);
}

}  // namespace record_play
}  // namespace examples
}  // namespace cyber
}  // namespace apollo

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  apollo::cyber::Init(argv[0]);
  const int ret = RUN_ALL_TESTS();
  apollo::cyber::AsyncShutdown();
  apollo::cyber::WaitForShutdown();
  return ret;
}
