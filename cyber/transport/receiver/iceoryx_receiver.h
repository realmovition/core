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

#ifndef CYBER_TRANSPORT_RECEIVER_ICEORYX_RECEIVER_H_
#define CYBER_TRANSPORT_RECEIVER_ICEORYX_RECEIVER_H_

#include <atomic>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <mutex>
#include <thread>
#include <type_traits>

#include "cyber/common/global_data.h"
#include "cyber/common/log.h"
#include "cyber/common/util.h"
#include "cyber/message/message_traits.h"
#include "cyber/transport/iceoryx_chunk.h"
#include "cyber/proto/transport_conf.pb.h"
#include "cyber/transport/iceoryx_runtime.h"
#include "cyber/transport/receiver/iceoryx_event_bridge.h"
#include "cyber/transport/receiver/receiver.h"
#include "iceoryx_posh/capro/service_description.hpp"
#include "iceoryx_posh/popo/subscriber.hpp"
#include "iceoryx_posh/popo/subscriber_options.hpp"
#include "iceoryx_posh/popo/wait_set.hpp"
#include "iox/duration.hpp"

namespace apollo {
namespace cyber {
namespace transport {

namespace {

inline iox::capro::ServiceDescription MakeIceoryxRxServiceDescription(
    const RoleAttributes& attr) {
  const iox::capro::IdString_t service{iox::TruncateToCapacity,
                                       std::to_string(attr.channel_id()).c_str()};
  const iox::capro::IdString_t instance{iox::TruncateToCapacity, "cyber"};
  const iox::capro::IdString_t event{
      iox::TruncateToCapacity,
      std::to_string(common::Hash(attr.channel_name())).c_str()};
  return {service, instance, event};
}

inline iox::popo::SubscriberOptions BuildSubscriberOptions(
    const RoleAttributes& attr) {
  iox::popo::SubscriberOptions options;
  const auto depth = static_cast<uint64_t>(
      std::max<int64_t>(1, attr.qos_profile().depth()));
  options.queueCapacity = depth;
  options.historyRequest = depth;
  if (!common::GlobalData::Instance()->Config().has_transport_conf()) {
    options.queueFullPolicy = iox::popo::QueueFullPolicy::DISCARD_OLDEST_DATA;
    return options;
  }
  switch (common::GlobalData::Instance()->Config().transport_conf()
              .slow_consumer_policy()) {
    case apollo::cyber::proto::DROP:
      options.queueFullPolicy = iox::popo::QueueFullPolicy::DISCARD_OLDEST_DATA;
      break;
    case apollo::cyber::proto::OVERWRITE:
      options.queueFullPolicy = iox::popo::QueueFullPolicy::DISCARD_OLDEST_DATA;
      break;
    default:
      options.queueFullPolicy = iox::popo::QueueFullPolicy::DISCARD_OLDEST_DATA;
      break;
  }
  return options;
}

}  // namespace

template <typename M>
class IceoryxReceiver : public Receiver<M> {
 public:
  IceoryxReceiver(const RoleAttributes& attr,
                  const typename Receiver<M>::MessageListener& msg_listener)
      : Receiver<M>(attr, msg_listener),
        runtime_bootstrap_(attr),
        service_(MakeIceoryxRxServiceDescription(attr)),
        subscriber_(std::make_shared<iox::popo::Subscriber<IceoryxByteChunk>>(
            service_, BuildSubscriberOptions(attr))) {}

  ~IceoryxReceiver() override { Disable(); }

  void Enable() override {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (running_.load()) {
      this->enabled_ = true;
      return;
    }
    running_.store(true);
    worker_ = std::thread(&IceoryxReceiver<M>::Run, this);
    this->enabled_ = true;
  }

  void Disable() override {
    std::thread worker_to_join;
    {
      std::lock_guard<std::mutex> lock(lifecycle_mutex_);
      running_.store(false);
      this->enabled_ = false;
      if (worker_.joinable()) {
        worker_to_join = std::move(worker_);
      }
    }
    if (worker_to_join.joinable()) {
      worker_to_join.join();
    }
  }

  void Enable(const RoleAttributes& opposite_attr) override {
    if (opposite_attr.channel_name() == this->attr_.channel_name()) {
      Enable();
    }
  }

  void Disable(const RoleAttributes& opposite_attr) override {
    if (opposite_attr.channel_name() == this->attr_.channel_name()) {
      Disable();
    }
  }

 private:
  struct RuntimeBootstrap {
    explicit RuntimeBootstrap(const RoleAttributes& attr) {
      EnsureIceoryxRuntime(attr);
    }
  };

  void Run() {
    using namespace iox::units::duration_literals;
    auto subscriber = subscriber_;
    iox::popo::WaitSet<1> waitset;
    auto attach_result =
        waitset.attachState(*subscriber, iox::popo::SubscriberState::HAS_DATA);
    if (attach_result.has_error()) {
      AERROR << "failed to attach iceoryx subscriber waitset for "
             << this->attr_.channel_name();
      return;
    }
    while (running_.load()) {
      if (subscriber->getSubscriptionState() != iox::SubscribeState::SUBSCRIBED) {
        waitset.timedWait(10_ms);
        continue;
      }

      bool received = false;
      for (const auto* notification : waitset.timedWait(100_ms)) {
        if (!notification->doesOriginateFrom(subscriber.get())) {
          continue;
        }
        do {
          received = false;
          subscriber->take().and_then([&](auto& sample) {
            auto msg = std::make_shared<M>();
            if constexpr (std::is_same<M, PodMessage>::value) {
              using SampleType = std::decay_t<decltype(sample)>;
              struct SampleOwner {
                using Subscriber = iox::popo::Subscriber<IceoryxByteChunk>;
                SampleOwner(std::shared_ptr<Subscriber> subscriber,
                            SampleType&& sample)
                    : subscriber_keepalive(std::move(subscriber)),
                      sample(std::move(sample)) {}
                std::shared_ptr<Subscriber> subscriber_keepalive;
                SampleType sample;
              };
              auto sample_owner = std::make_shared<SampleOwner>(
                  subscriber, std::move(sample));
              if (!DecodeMessageFromIceoryxChunk(*sample_owner->sample.get(),
                                                 sample_owner, msg.get())) {
                return;
              }
            } else {
              if (!DecodeMessageFromIceoryxChunk(*sample, msg.get())) {
                return;
              }
            }
            this->OnNewMessage(msg, MessageInfo());
            received = true;
          }).or_else([&](auto&) {
            received = false;
          });
        } while (running_.load() && received);
      }
    }
    waitset.markForDestruction();
  }

  RuntimeBootstrap runtime_bootstrap_;
  iox::capro::ServiceDescription service_;
  std::shared_ptr<iox::popo::Subscriber<IceoryxByteChunk>> subscriber_;
  std::atomic<bool> running_ = {false};
  std::thread worker_;
  std::mutex lifecycle_mutex_;
};

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_TRANSPORT_RECEIVER_ICEORYX_RECEIVER_H_
