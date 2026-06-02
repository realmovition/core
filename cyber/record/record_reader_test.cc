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

#include "cyber/record/record_reader.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "cyber/examples/proto/examples_generated.h"
#include "cyber/message/flatbuffers_message.h"
#include "cyber/record/record_writer.h"

namespace apollo {
namespace cyber {
namespace record {

using apollo::cyber::message::FlatBufferMessage;
using apollo::cyber::message::RawMessage;

constexpr char kChannelName1[] = "/test/channel1";
constexpr char kMessageType1[] = "apollo.cyber.proto.Test";
constexpr char kProtoDesc[] = "1234567890";
constexpr char kStr10B[] = "1234567890";
constexpr char kTestFile[] = "record_reader_test.record";
constexpr char kFlatBufferChannelName[] = "/test/channel_flatbuffer";
constexpr char kFlatBufferMessageType[] = "apollo.cyber.examples.proto.Chatter";
constexpr char kFlatBufferTestFile[] = "record_reader_flatbuffer_test.record";
constexpr uint32_t kMessageNum = 16;

static std::vector<uint8_t> BuildChatterBuffer(uint64_t seq,
                                               const char* content) {
  flatbuffers::FlatBufferBuilder fbb;
  auto chatter =
      examples::proto::CreateChatterDirect(fbb, 1000, 2000, seq, content);
  fbb.Finish(chatter);
  return {fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};
}

TEST(RecordTest, TestSingleRecordFile) {
  RecordWriter writer;
  writer.SetSizeOfFileSegmentation(0);
  writer.SetIntervalOfFileSegmentation(0);
  writer.Open(kTestFile);
  writer.WriteChannel(kChannelName1, kMessageType1, kProtoDesc);
  for (uint32_t i = 0; i < kMessageNum; ++i) {
    auto msg = std::make_shared<RawMessage>(std::to_string(i));
    writer.WriteMessage(kChannelName1, msg, i);
  }
  ASSERT_EQ(kMessageNum, writer.GetMessageNumber(kChannelName1));
  ASSERT_EQ(kMessageType1, writer.GetMessageType(kChannelName1));
  ASSERT_EQ(kProtoDesc, writer.GetProtoDesc(kChannelName1));
  writer.Close();

  RecordReader reader(kTestFile);
  RecordMessage message;
  ASSERT_EQ(kMessageNum, reader.GetMessageNumber(kChannelName1));
  ASSERT_EQ(kMessageType1, reader.GetMessageType(kChannelName1));
  ASSERT_EQ(kProtoDesc, reader.GetProtoDesc(kChannelName1));

  // read all message
  uint32_t i = 0;
  for (i = 0; i < kMessageNum; ++i) {
    ASSERT_TRUE(reader.ReadMessage(&message));
    ASSERT_EQ(kChannelName1, message.channel_name);
    ASSERT_EQ(std::to_string(i), message.content);
    ASSERT_EQ(i, message.time);
  }
  ASSERT_FALSE(reader.ReadMessage(&message));

  // skip first message
  reader.Reset();
  for (i = 0; i < kMessageNum - 1; ++i) {
    ASSERT_TRUE(reader.ReadMessage(&message, 1));
    ASSERT_EQ(kChannelName1, message.channel_name);
    ASSERT_EQ(std::to_string(i + 1), message.content);
    ASSERT_EQ(i + 1, message.time);
  }
  ASSERT_FALSE(reader.ReadMessage(&message, 1));

  // skip last message
  reader.Reset();
  for (i = 0; i < kMessageNum - 1; ++i) {
    ASSERT_TRUE(reader.ReadMessage(&message, 0, kMessageNum - 2));
    ASSERT_EQ(kChannelName1, message.channel_name);
    ASSERT_EQ(std::to_string(i), message.content);
    ASSERT_EQ(i, message.time);
  }
  ASSERT_FALSE(reader.ReadMessage(&message, 0, kMessageNum - 2));
  ASSERT_FALSE(remove(kTestFile));
}

TEST(RecordTest, TestFlatBufferRecordMetadataUsesConcreteTypeName) {
  auto buf = BuildChatterBuffer(7, "record-flatbuffer");
  FlatBufferMessage message(kFlatBufferMessageType, buf.data(), buf.size());

  RecordWriter writer;
  writer.SetSizeOfFileSegmentation(0);
  writer.SetIntervalOfFileSegmentation(0);
  writer.Open(kFlatBufferTestFile);
  ASSERT_TRUE(writer.WriteMessage(kFlatBufferChannelName, message, 123u));
  ASSERT_EQ(1u, writer.GetMessageNumber(kFlatBufferChannelName));
  ASSERT_EQ(kFlatBufferMessageType,
            writer.GetMessageType(kFlatBufferChannelName));
  EXPECT_EQ("", writer.GetProtoDesc(kFlatBufferChannelName));
  writer.Close();

  RecordReader reader(kFlatBufferTestFile);
  ASSERT_EQ(1u, reader.GetMessageNumber(kFlatBufferChannelName));
  ASSERT_EQ(kFlatBufferMessageType,
            reader.GetMessageType(kFlatBufferChannelName));
  EXPECT_EQ("", reader.GetProtoDesc(kFlatBufferChannelName));

  RecordMessage record_message;
  ASSERT_TRUE(reader.ReadMessage(&record_message));
  EXPECT_EQ(kFlatBufferChannelName, record_message.channel_name);
  EXPECT_EQ(123u, record_message.time);

  FlatBufferMessage decoded(
      kFlatBufferMessageType,
      reinterpret_cast<const uint8_t*>(record_message.content.data()),
      record_message.content.size());
  const auto* chatter = decoded.GetRoot<examples::proto::Chatter>();
  ASSERT_NE(chatter, nullptr);
  EXPECT_EQ(chatter->seq(), 7u);
  EXPECT_STREQ(chatter->content()->c_str(), "record-flatbuffer");
  ASSERT_FALSE(reader.ReadMessage(&record_message));
  ASSERT_FALSE(remove(kFlatBufferTestFile));
}

TEST(RecordTest, TestReaderOrder) {
  RecordWriter writer;
  writer.SetSizeOfFileSegmentation(0);
  writer.SetIntervalOfFileSegmentation(0);
  writer.Open(kTestFile);
  writer.WriteChannel(kChannelName1, kMessageType1, kProtoDesc);

  for (uint32_t i = kMessageNum; i > 0; --i) {
    auto msg = std::make_shared<RawMessage>(std::to_string(i));
    writer.WriteMessage(kChannelName1, msg, i * 100);
  }
  ASSERT_EQ(kMessageNum, writer.GetMessageNumber(kChannelName1));
  ASSERT_EQ(kMessageType1, writer.GetMessageType(kChannelName1));
  ASSERT_EQ(kProtoDesc, writer.GetProtoDesc(kChannelName1));
  writer.Close();

  RecordReader reader(kTestFile);
  RecordMessage message;
  ASSERT_EQ(kMessageNum, reader.GetMessageNumber(kChannelName1));
  ASSERT_EQ(kMessageType1, reader.GetMessageType(kChannelName1));
  ASSERT_EQ(kProtoDesc, reader.GetProtoDesc(kChannelName1));

  // read all message
  for (uint32_t i = 1; i <= kMessageNum; ++i) {
    ASSERT_TRUE(reader.ReadMessage(&message));
    ASSERT_EQ(kChannelName1, message.channel_name);
    ASSERT_NE(std::to_string(i), message.content);
    ASSERT_NE(i * 100, message.time);
  }

  ASSERT_FALSE(reader.ReadMessage(&message));
  ASSERT_FALSE(remove(kTestFile));
}

}  // namespace record
}  // namespace cyber
}  // namespace apollo
