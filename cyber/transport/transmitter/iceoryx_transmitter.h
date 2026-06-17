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

#ifndef CYBER_TRANSPORT_TRANSMITTER_ICEORYX_TRANSMITTER_H_
#define CYBER_TRANSPORT_TRANSMITTER_ICEORYX_TRANSMITTER_H_

#include <functional>
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

#include "cyber/common/global_data.h"
#include "cyber/common/log.h"
#include "cyber/common/util.h"
#include "cyber/message/message_traits.h"
#include "cyber/transport/iceoryx_chunk.h"
#include "cyber/proto/transport_conf.pb.h"
#include "cyber/transport/iceoryx_runtime.h"
#include "cyber/transport/shm/profile.h"
#include "cyber/transport/transmitter/transmitter.h"
#include "iceoryx_posh/capro/service_description.hpp"
#include "iceoryx_posh/popo/publisher.hpp"
#include "iceoryx_posh/popo/publisher_options.hpp"

namespace apollo {
namespace cyber {
namespace transport {

namespace {

inline iox::capro::ServiceDescription MakeIceoryxTxServiceDescription(
    const RoleAttributes& attr) {
  const iox::capro::IdString_t service{iox::TruncateToCapacity,
                                       std::to_string(attr.channel_id()).c_str()};
  const iox::capro::IdString_t instance{iox::TruncateToCapacity, "cyber"};
  const iox::capro::IdString_t event{
      iox::TruncateToCapacity,
      std::to_string(common::Hash(attr.channel_name())).c_str()};
  return {service, instance, event};
}

inline iox::popo::PublisherOptions BuildPublisherOptions(
    const RoleAttributes& attr) {
  iox::popo::PublisherOptions options;
  const auto depth = static_cast<uint64_t>(
      std::max<int64_t>(1, attr.qos_profile().depth()));
  options.historyCapacity = depth;
  if (!common::GlobalData::Instance()->Config().has_transport_conf()) {
    options.subscriberTooSlowPolicy =
        iox::popo::ConsumerTooSlowPolicy::DISCARD_OLDEST_DATA;
    return options;
  }
  switch (common::GlobalData::Instance()->Config().transport_conf()
              .slow_consumer_policy()) {
    case apollo::cyber::proto::DROP:
      options.subscriberTooSlowPolicy =
          iox::popo::ConsumerTooSlowPolicy::DISCARD_OLDEST_DATA;
      break;
    case apollo::cyber::proto::OVERWRITE:
      options.subscriberTooSlowPolicy =
          iox::popo::ConsumerTooSlowPolicy::DISCARD_OLDEST_DATA;
      break;
    default:
      options.subscriberTooSlowPolicy =
          iox::popo::ConsumerTooSlowPolicy::DISCARD_OLDEST_DATA;
      break;
  }
  return options;
}

class IceoryxLoanAttachment : public LoanAttachment {
 public:
  using Sample = iox::popo::Sample<IceoryxByteChunk>;

  explicit IceoryxLoanAttachment(Sample&& sample)
      : sample_(std::make_unique<Sample>(std::move(sample))) {}

  uint8_t* data() {
    return sample_ == nullptr ? nullptr : sample_->get()->payload.data();
  }

  std::size_t capacity() const {
    return sample_ == nullptr ? 0 : sample_->get()->payload.size();
  }

  bool Publish(std::size_t size) {
    if (published_ || sample_ == nullptr || size > capacity()) {
      return false;
    }
    sample_->get()->payload_size = static_cast<uint32_t>(size);
    sample_->publish();
    sample_.reset();
    published_ = true;
    return true;
  }

 private:
  std::unique_ptr<Sample> sample_;
  bool published_ = false;
};

}  // namespace

template <typename M>
class IceoryxTransmitter : public Transmitter<M> {
 public:
  using MessagePtr = std::shared_ptr<M>;

  explicit IceoryxTransmitter(const RoleAttributes& attr)
      : Transmitter<M>(attr),
        runtime_bootstrap_(attr),
        service_(MakeIceoryxTxServiceDescription(attr)),
        publisher_(service_, BuildPublisherOptions(attr)) {}

  ~IceoryxTransmitter() override { Disable(); }

  void Enable() override { this->enabled_ = true; }

  void Disable() override { this->enabled_ = false; }

  bool Transmit(const MessagePtr& msg, const MessageInfo& msg_info) override {
    (void)msg_info;
    if (!this->enabled_ || msg == nullptr) {
      return false;
    }
    if (!message::HasSerializer<M>::value) {
      return false;
    }
    const auto byte_size = message::ByteSize(*msg);
    if (byte_size < 0) {
      return false;
    }
    if (static_cast<std::size_t>(byte_size) > kIceoryxChunkPayloadCapacity) {
      return false;
    }
    TransportProfileRecorder::Instance()->Record(
        this->attr_.channel_id(), static_cast<std::size_t>(byte_size));
    bool encoded = false;
    auto result = publisher_.publishResultOf(
        [&msg, &encoded](IceoryxByteChunk* chunk) {
          encoded = EncodeMessageToIceoryxChunk(*msg, chunk);
        });
    return !result.has_error() && encoded;
  }

  bool IsLoanSupported() const override { return true; }

  bool Loan(std::size_t size, LoanedMessage<M>* loaned_msg) override {
    if (!this->enabled_ || loaned_msg == nullptr ||
        size > kIceoryxChunkPayloadCapacity) {
      return false;
    }
    auto loan_result = publisher_.loan();
    if (loan_result.has_error()) {
      return false;
    }
    auto sample = std::move(loan_result).value();
    auto attachment =
        std::make_shared<IceoryxLoanAttachment>(std::move(sample));
    loaned_msg->SetBuffer(attachment->data(), attachment->capacity());
    loaned_msg->SetAttachment(attachment);
    return loaned_msg->valid();
  }

  bool Publish(LoanedMessage<M>&& loaned_msg) override {
    if (!loaned_msg.valid()) {
      return false;
    }
    auto attachment =
        std::dynamic_pointer_cast<IceoryxLoanAttachment>(loaned_msg.attachment_);
    if (attachment == nullptr || !attachment->Publish(loaned_msg.size())) {
      return false;
    }
    loaned_msg.Reset();
    return true;
  }

 private:
  struct RuntimeBootstrap {
    explicit RuntimeBootstrap(const RoleAttributes& attr) {
      EnsureIceoryxRuntime(attr);
    }
  };

  RuntimeBootstrap runtime_bootstrap_;
  iox::capro::ServiceDescription service_;
  iox::popo::Publisher<IceoryxByteChunk> publisher_;
};

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_TRANSPORT_TRANSMITTER_ICEORYX_TRANSMITTER_H_
