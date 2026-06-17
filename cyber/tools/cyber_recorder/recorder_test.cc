// Copyright 2026 WheelOS. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "gtest/gtest.h"

#include "cyber/cyber.h"
#include "cyber/record/header_builder.h"
#include "cyber/record/record_reader.h"
#include "cyber/tools/cyber_recorder/recorder.h"
#include "cyber/transport/message/pod_message.h"

namespace apollo {
namespace cyber {
namespace record {
namespace {

constexpr char kRecordPath[] = "pod_recorder_sink_test.record";
constexpr char kChannelName[] = "/test/pod_recorder_sink";

bool WaitForReader(const std::shared_ptr<Writer<transport::PodMessage>>& writer) {
  for (int i = 0; i < 100; ++i) {
    if (writer->HasReader()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

}  // namespace

TEST(RecorderTest, RecordsPodMessageWithSchema) {
  std::remove(kRecordPath);

  auto record_header = HeaderBuilder::GetHeaderWithSegmentParams(0, 0);
  auto recorder =
      std::make_shared<Recorder>(kRecordPath, true, std::vector<std::string>{},
                                 std::vector<std::string>{}, record_header);
  ASSERT_TRUE(recorder->Start());

  auto node = CreateNode("pod_recorder_sink_test_" + std::to_string(getpid()));
  ASSERT_NE(node, nullptr);
  auto writer = node->CreateWriter<transport::PodMessage>(kChannelName);
  ASSERT_NE(writer, nullptr);
  ASSERT_TRUE(WaitForReader(writer));

  const char payload[] = "pod_recorder_sink_payload";
  auto pod_header = transport::MakeImagePodChunkHeader(
      123456789ull, 11ull, 2u, 2u, 4u, 1u, sizeof(payload));
  auto msg =
      std::make_shared<transport::PodMessage>(pod_header, payload,
                                              sizeof(payload));
  ASSERT_TRUE(writer->Write(msg));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  ASSERT_TRUE(recorder->Stop());
  node.reset();

  RecordReader reader(kRecordPath);
  ASSERT_TRUE(reader.IsValid());
  ASSERT_EQ(reader.GetMessageType(kChannelName),
            transport::PodMessage::TypeName());
  ASSERT_TRUE(transport::IsPodSchemaDescriptor(reader.GetProtoDesc(kChannelName)));
  EXPECT_EQ(reader.GetMessageNumber(kChannelName), 1U);

  RecordMessage record_msg;
  ASSERT_TRUE(reader.ReadMessage(&record_msg));
  EXPECT_EQ(record_msg.channel_name, kChannelName);
  transport::PodMessage pod;
  ASSERT_TRUE(pod.ParseFromString(record_msg.content));
  const auto view = pod.View();
  ASSERT_NE(view.payload, nullptr);
  EXPECT_EQ(view.header.frame_id, 11ull);
  EXPECT_EQ(view.payload_size, sizeof(payload));
  EXPECT_EQ(std::memcmp(view.payload, payload, sizeof(payload)), 0);

  std::remove(kRecordPath);
}

}  // namespace record
}  // namespace cyber
}  // namespace apollo

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  apollo::cyber::Init(argv[0]);
  const int ret = RUN_ALL_TESTS();
  apollo::cyber::Clear();
  return ret;
}
