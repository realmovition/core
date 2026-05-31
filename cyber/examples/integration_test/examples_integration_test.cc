// Copyright 2025 WheelOS. All Rights Reserved.
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

//  Created Date: 2026-05-31
//  Author: daohu527


#include "cyber/cyber.h"
#include "cyber/examples/proto/examples.pb.h"
#include "cyber/node/node_channel_impl.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "gtest/gtest.h"

namespace apollo {
namespace cyber {
namespace examples {

namespace {

using apollo::cyber::examples::proto::Chatter;
using apollo::cyber::examples::proto::Driver;

std::string UniqueName(const std::string& prefix) {
  static std::atomic<uint64_t> counter{0};
  return prefix + "_" + std::to_string(counter.fetch_add(1));
}

template <typename Predicate>
bool WaitFor(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

class ExamplesIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    channel_name_ = UniqueName("channel_chatter");
    service_name_ = UniqueName("test_server");
  }

  std::string channel_name_;
  std::string service_name_;
};

TEST_F(ExamplesIntegrationTest, TalkerListenerRoundTrip) {
  std::mutex mutex;
  std::condition_variable cv;
  bool received = false;
  Chatter received_msg;

  auto listener_node = apollo::cyber::CreateNode(UniqueName("listener"));
  auto reader = listener_node->CreateReader<Chatter>(
      channel_name_,
      [&](const std::shared_ptr<Chatter>& msg) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          received_msg = *msg;
          received = true;
        }
        cv.notify_one();
      });
  ASSERT_NE(reader, nullptr);

  auto talker_node = apollo::cyber::CreateNode(UniqueName("talker"));
  auto writer = talker_node->CreateWriter<Chatter>(channel_name_);
  ASSERT_NE(writer, nullptr);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto msg = std::make_shared<Chatter>();
  msg->set_seq(7);
  msg->set_content("Hello, apollo!");
  msg->set_timestamp(123);
  msg->set_lidar_timestamp(456);

  ASSERT_TRUE(writer->Write(msg));

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3),
                          [&received]() { return received; }));
  EXPECT_EQ(received_msg.seq(), 7U);
  EXPECT_EQ(received_msg.content(), "Hello, apollo!");
  EXPECT_EQ(received_msg.timestamp(), 123U);
  EXPECT_EQ(received_msg.lidar_timestamp(), 456U);
}

TEST_F(ExamplesIntegrationTest, TalkerListenerPerformanceSmoke) {
  constexpr size_t kMessageCount = 64;
  std::mutex mutex;
  std::condition_variable cv;
  size_t received_count = 0;
  auto first_receive = std::chrono::steady_clock::time_point{};
  auto last_receive = std::chrono::steady_clock::time_point{};

  auto listener_node = apollo::cyber::CreateNode(UniqueName("perf_listener"));
  apollo::cyber::ReaderConfig reader_config;
  reader_config.channel_name = channel_name_;
  reader_config.pending_queue_size = kMessageCount;
  reader_config.qos_profile.set_depth(kMessageCount);
  auto reader = listener_node->CreateReader<Chatter>(
      reader_config,
      [&](const std::shared_ptr<Chatter>& msg) {
        (void)msg;
        std::lock_guard<std::mutex> lock(mutex);
        const auto now = std::chrono::steady_clock::now();
        if (received_count == 0) {
          first_receive = now;
        }
        ++received_count;
        last_receive = now;
        if (received_count >= kMessageCount) {
          cv.notify_one();
        }
      });
  ASSERT_NE(reader, nullptr);

  auto talker_node = apollo::cyber::CreateNode(UniqueName("perf_talker"));
  auto writer = talker_node->CreateWriter<Chatter>(channel_name_);
  ASSERT_NE(writer, nullptr);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < kMessageCount; ++i) {
    auto msg = std::make_shared<Chatter>();
    msg->set_seq(i);
    msg->set_content("perf");
    msg->set_timestamp(i);
    msg->set_lidar_timestamp(i);
    ASSERT_TRUE(writer->Write(msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5),
                          [&received_count]() {
                            return received_count >= kMessageCount;
                          }));

  const auto end_to_end =
      std::chrono::duration_cast<std::chrono::milliseconds>(last_receive -
                                                            start);
  const auto receive_window =
      std::chrono::duration_cast<std::chrono::milliseconds>(last_receive -
                                                            first_receive);
  EXPECT_LT(end_to_end.count(), 5000);
  EXPECT_LT(receive_window.count(), 5000);
}

TEST_F(ExamplesIntegrationTest, ServiceClientRoundTrip) {
  auto server_node = apollo::cyber::CreateNode(UniqueName("service"));
  auto server = server_node->CreateService<Driver, Driver>(
      service_name_,
      [](const std::shared_ptr<Driver>& request,
         std::shared_ptr<Driver>& response) {
        response->set_content(request->content() + "_reply");
        response->set_msg_id(request->msg_id() + 1);
        response->set_timestamp(request->timestamp() + 10);
      });
  ASSERT_NE(server, nullptr);

  auto client_node = apollo::cyber::CreateNode(UniqueName("client"));
  auto client = client_node->CreateClient<Driver, Driver>(service_name_);
  ASSERT_NE(client, nullptr);

  auto request = std::make_shared<Driver>();
  request->set_content("ping");
  request->set_msg_id(41);
  request->set_timestamp(100);

  std::shared_ptr<Driver> response;
  ASSERT_TRUE(WaitFor(
      [&]() {
        response = client->SendRequest(request);
        return response != nullptr;
      },
      std::chrono::seconds(3)));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->content(), "ping_reply");
  EXPECT_EQ(response->msg_id(), 42U);
  EXPECT_EQ(response->timestamp(), 110U);
}

}  // namespace

}  // namespace examples
}  // namespace cyber
}  // namespace apollo

int main(int argc, char** argv) {
  const auto cyber_path = (std::filesystem::current_path() / "cyber").string();
  setenv("CYBER_PATH", cyber_path.c_str(), 1);
  testing::InitGoogleTest(&argc, argv);
  apollo::cyber::Init(argv[0]);
  const int result = RUN_ALL_TESTS();
  // Cyber currently crashes during process teardown in this integration-test
  // process, after gtest has already reported the result.
  std::fflush(nullptr);
  _Exit(result);
}
