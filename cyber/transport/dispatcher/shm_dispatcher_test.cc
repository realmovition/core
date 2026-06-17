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

#include "cyber/transport/dispatcher/shm_dispatcher.h"

#include <cstring>
#include <memory>
#include "gtest/gtest.h"

#include "cyber/common/global_data.h"
#include "cyber/common/log.h"
#include "cyber/common/util.h"
#include "cyber/init.h"
#include "cyber/message/raw_message.h"
#include "cyber/proto/unit_test.pb.h"
#include "cyber/transport/common/identity.h"
#include "cyber/transport/message/pod_message.h"
#include "cyber/transport/transport.h"

#include <vector>

namespace apollo {
namespace cyber {
namespace transport {

TEST(ShmDispatcherTest, add_listener) {
  auto dispatcher = ShmDispatcher::Instance();
  RoleAttributes self_attr;
  self_attr.set_channel_name("add_listener");
  self_attr.set_channel_id(common::Hash("add_listener"));
  Identity self_id;
  self_attr.set_id(self_id.HashValue());

  dispatcher->AddListener<proto::Chatter>(
      self_attr,
      [](const std::shared_ptr<proto::Chatter>&, const MessageInfo&) {});

  RoleAttributes oppo_attr;
  oppo_attr.CopyFrom(self_attr);
  Identity oppo_id;
  oppo_attr.set_id(oppo_id.HashValue());

  dispatcher->AddListener<proto::Chatter>(
      self_attr, oppo_attr,
      [](const std::shared_ptr<proto::Chatter>&, const MessageInfo&) {});
}

TEST(ShmDispatcherTest, on_message) {
  auto dispatcher = ShmDispatcher::Instance();

  RoleAttributes oppo_attr;
  oppo_attr.set_host_name(common::GlobalData::Instance()->HostName());
  oppo_attr.set_host_ip(common::GlobalData::Instance()->HostIp());
  oppo_attr.set_channel_name("on_message");
  oppo_attr.set_channel_id(common::Hash("on_message"));
  Identity oppo_id;
  oppo_attr.set_id(oppo_id.HashValue());

  auto transmitter =
      Transport::Instance()->CreateTransmitter<message::RawMessage>(
          oppo_attr, proto::OptionalMode::SHM);
  EXPECT_NE(transmitter, nullptr);

  auto send_msg = std::make_shared<message::RawMessage>("raw_message");
  transmitter->Transmit(send_msg);

  sleep(1);

  RoleAttributes self_attr;
  self_attr.set_channel_name("on_message");
  self_attr.set_channel_id(common::Hash("on_message"));
  Identity self_id;
  self_attr.set_id(self_id.HashValue());

  auto recv_msg = std::make_shared<message::RawMessage>();
  dispatcher->AddListener<message::RawMessage>(
      self_attr, [&recv_msg](const std::shared_ptr<message::RawMessage>& msg,
                             const MessageInfo& msg_info) {
        (void)msg_info;
        recv_msg->message = msg->message;
      });

  transmitter->Transmit(send_msg);

  sleep(1);
  EXPECT_EQ(recv_msg->message, send_msg->message);
}

TEST(ShmDispatcherTest, pod_message_zero_copy_read_from_shm) {
  constexpr char kChannelName[] = "pod_message_zero_copy_read_from_shm";
  constexpr char kMessageType[] = "apollo.cyber.transport.PodMessage";

  auto dispatcher = ShmDispatcher::Instance();

  RoleAttributes self_attr;
  self_attr.set_channel_name(kChannelName);
  self_attr.set_channel_id(common::Hash(kChannelName));
  self_attr.set_message_type(kMessageType);
  self_attr.set_host_name(common::GlobalData::Instance()->HostName());
  self_attr.set_host_ip(common::GlobalData::Instance()->HostIp());
  Identity self_id;
  self_attr.set_id(self_id.HashValue());

  std::shared_ptr<PodMessage> recv_msg;
  dispatcher->AddListener<PodMessage>(
      self_attr,
      [&recv_msg](const std::shared_ptr<PodMessage>& msg, const MessageInfo&) {
        recv_msg = msg;
      });

  RoleAttributes oppo_attr;
  oppo_attr.CopyFrom(self_attr);
  oppo_attr.set_host_name(common::GlobalData::Instance()->HostName());
  oppo_attr.set_host_ip(common::GlobalData::Instance()->HostIp());
  Identity oppo_id;
  oppo_attr.set_id(oppo_id.HashValue());

  auto transmitter =
      Transport::Instance()->CreateTransmitter<PodMessage>(
          oppo_attr, proto::OptionalMode::SHM);
  ASSERT_NE(transmitter, nullptr);

  const std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 0, 7, 8};
  const auto header = MakeImagePodChunkHeader(
      /*timestamp_ns=*/123, /*frame_id=*/99, /*width=*/2, /*height=*/4,
      /*stride_bytes=*/8, /*pixel_format=*/24,
      static_cast<uint32_t>(payload.size()));

  LoanedMessage<PodMessage> loaned;
  ASSERT_TRUE(transmitter->Loan(PodChunkTotalSize(payload.size()), &loaned));
  std::size_t written = 0;
  ASSERT_TRUE(BuildPodChunk(header, payload.data(), payload.size(),
                            loaned.data(), loaned.capacity(), &written));
  ASSERT_EQ(written, PodChunkTotalSize(payload.size()));
  ASSERT_TRUE(loaned.set_size(PodChunkTotalSize(payload.size())));
  ASSERT_TRUE(transmitter->Publish(std::move(loaned)));

  sleep(1);
  ASSERT_NE(recv_msg, nullptr);
  ASSERT_NE(recv_msg->header(), nullptr);
  EXPECT_EQ(recv_msg->header()->magic, PodChunkHeader::kMagic);
  EXPECT_EQ(recv_msg->header()->frame_id, 99u);
  EXPECT_EQ(recv_msg->header()->payload_size, payload.size());
  PodChunkView view = recv_msg->View();
  ASSERT_NE(view.payload, nullptr);
  EXPECT_EQ(0, std::memcmp(view.payload, payload.data(), payload.size()));
}

TEST(ShmDispatcherTest, shutdown) {
  auto dispatcher = ShmDispatcher::Instance();
  dispatcher->Shutdown();

  // repeated call
  dispatcher->Shutdown();
}

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  apollo::cyber::Init(argv[0]);
  apollo::cyber::transport::Transport::Instance();
  auto res = RUN_ALL_TESTS();
  apollo::cyber::transport::Transport::Instance()->Shutdown();
  return res;
}
