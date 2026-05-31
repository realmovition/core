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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

class ExamplesStressTest : public ::testing::Test {
 protected:
  void SetUp() override {
    channel_name_ = UniqueName("stress_channel");
    service_name_ = UniqueName("stress_service");
  }

  std::string channel_name_;
  std::string service_name_;
};

TEST_F(ExamplesStressTest, TalkerListenerSustainedLoad) {
  constexpr size_t kMessageCount = 256;
  constexpr size_t kPayloadSize = 2048;
  constexpr size_t kQueueDepth = kMessageCount * 2;
  constexpr auto kTimeout = std::chrono::seconds(20);
  constexpr uint64_t kWarmupSeq = std::numeric_limits<uint64_t>::max();

  std::mutex mutex;
  std::condition_variable cv;
  std::vector<bool> seen(kMessageCount, false);
  bool warmup_received = false;
  size_t unique_received = 0;
  size_t duplicate_received = 0;
  size_t payload_mismatch = 0;
  auto last_receive = std::chrono::steady_clock::time_point{};

  auto listener_node = apollo::cyber::CreateNode(UniqueName("stress_listener"));
  apollo::cyber::ReaderConfig reader_config;
  reader_config.channel_name = channel_name_;
  reader_config.pending_queue_size = kQueueDepth;
  reader_config.qos_profile.set_depth(kQueueDepth);
  auto reader = listener_node->CreateReader<Chatter>(
      reader_config,
      [&](const std::shared_ptr<Chatter>& msg) {
        std::lock_guard<std::mutex> lock(mutex);
        if (msg->seq() == kWarmupSeq) {
          warmup_received = true;
          cv.notify_all();
          return;
        }
        if (msg->content().size() != kPayloadSize) {
          ++payload_mismatch;
        }
        const auto seq = static_cast<size_t>(msg->seq());
        if (seq < kMessageCount) {
          if (!seen[seq]) {
            seen[seq] = true;
            ++unique_received;
            if (unique_received == kMessageCount) {
              last_receive = std::chrono::steady_clock::now();
              cv.notify_one();
            }
          } else {
            ++duplicate_received;
          }
        }
      });
  ASSERT_NE(reader, nullptr);

  auto talker_node = apollo::cyber::CreateNode(UniqueName("stress_talker"));
  auto writer = talker_node->CreateWriter<Chatter>(channel_name_);
  ASSERT_NE(writer, nullptr);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  for (size_t attempt = 0; attempt < 30 && !warmup_received; ++attempt) {
    auto warmup = std::make_shared<Chatter>();
    warmup->set_seq(kWarmupSeq);
    warmup->set_content("warmup");
    ASSERT_TRUE(writer->Write(warmup));

    std::unique_lock<std::mutex> warmup_lock(mutex);
    if (cv.wait_for(warmup_lock, std::chrono::milliseconds(100),
                    [&]() { return warmup_received; })) {
      break;
    }
  }
  ASSERT_TRUE(warmup_received);

  const std::string payload(kPayloadSize, 'x');
  const auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < kMessageCount; ++i) {
    auto msg = std::make_shared<Chatter>();
    msg->set_seq(i);
    msg->set_content(payload);
    msg->set_timestamp(i);
    msg->set_lidar_timestamp(i);
    ASSERT_TRUE(writer->Write(msg));
    if ((i + 1) % 8 == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(
      cv.wait_for(lock, kTimeout, [&]() { return unique_received == kMessageCount; }));
  ASSERT_NE(last_receive, std::chrono::steady_clock::time_point{});
  EXPECT_EQ(duplicate_received, 0U);
  EXPECT_EQ(payload_mismatch, 0U);

  const auto end_to_end =
      std::chrono::duration_cast<std::chrono::milliseconds>(last_receive - start);
  const double throughput =
      (1000.0 * static_cast<double>(kMessageCount)) /
      static_cast<double>(std::max<int64_t>(1, end_to_end.count()));
  EXPECT_LT(end_to_end.count(), 20000);
  EXPECT_GT(throughput, 20.0);
}

TEST_F(ExamplesStressTest, ServiceBurstRequests) {
  constexpr size_t kRequestCount = 64;

  auto server_node = apollo::cyber::CreateNode(UniqueName("stress_service"));
  auto server = server_node->CreateService<Driver, Driver>(
      service_name_,
      [](const std::shared_ptr<Driver>& request,
         std::shared_ptr<Driver>& response) {
        response->set_content(request->content() + "_reply");
        response->set_msg_id(request->msg_id() + 1000);
        response->set_timestamp(request->timestamp() + 10);
      });
  ASSERT_NE(server, nullptr);

  auto client_node = apollo::cyber::CreateNode(UniqueName("stress_client"));
  auto client = client_node->CreateClient<Driver, Driver>(service_name_);
  ASSERT_NE(client, nullptr);

  std::shared_ptr<Driver> warmup_response;
  auto warmup_request = std::make_shared<Driver>();
  warmup_request->set_content("warmup");
  warmup_request->set_msg_id(1);
  warmup_request->set_timestamp(1);
  ASSERT_TRUE(WaitFor(
      [&]() {
        warmup_response = client->SendRequest(warmup_request);
        return warmup_response != nullptr;
      },
      std::chrono::seconds(3)));

  const auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < kRequestCount; ++i) {
    auto request = std::make_shared<Driver>();
    request->set_content("burst_" + std::to_string(i));
    request->set_msg_id(i);
    request->set_timestamp(i);
    auto response = client->SendRequest(request);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->content(), request->content() + "_reply");
    EXPECT_EQ(response->msg_id(), request->msg_id() + 1000);
    EXPECT_EQ(response->timestamp(), request->timestamp() + 10);
  }

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
  EXPECT_LT(elapsed.count(), 10000);
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
  std::fflush(nullptr);
  _Exit(result);
}
