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

#ifndef CYBER_TRANSPORT_TRANSMITTER_SHM_TRANSMITTER_H_
#define CYBER_TRANSPORT_TRANSMITTER_SHM_TRANSMITTER_H_

#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "cyber/common/global_data.h"
#include "cyber/common/log.h"
#include "cyber/common/util.h"
#include "cyber/message/message_traits.h"
#include "cyber/transport/shm/profile.h"
#include "cyber/transport/shm/notifier_factory.h"
#include "cyber/transport/shm/readable_info.h"
#include "cyber/transport/shm/segment_factory.h"
#include "cyber/transport/transmitter/transmitter.h"

namespace apollo {
namespace cyber {
namespace transport {

template <typename M>
class ShmTransmitter : public Transmitter<M> {
 public:
  using MessagePtr = std::shared_ptr<M>;

  explicit ShmTransmitter(const RoleAttributes& attr);
  virtual ~ShmTransmitter();

  void Enable() override;
  void Disable() override;

  bool Transmit(const MessagePtr& msg, const MessageInfo& msg_info) override;
  bool IsLoanSupported() const override { return this->enabled_; }
  bool Loan(std::size_t size, LoanedMessage<M>* loaned_msg) override;
  bool Publish(LoanedMessage<M>&& loaned_msg) override;

 private:
  class ShmLoanAttachment : public LoanAttachment {
   public:
    ShmLoanAttachment(const SegmentPtr& segment, const WritableBlock& wb)
        : segment_(segment), wb_(wb) {}

    ~ShmLoanAttachment() override {
      if (!published_ && segment_ != nullptr) {
        segment_->ReleaseWrittenBlock(wb_);
      }
    }

    SegmentPtr segment_;
    WritableBlock wb_;
    bool published_ = false;
  };

  bool Transmit(const M& msg, const MessageInfo& msg_info);

  SegmentPtr segment_;
  uint64_t channel_id_;
  uint64_t host_id_;
  NotifierPtr notifier_;
};

template <typename M>
ShmTransmitter<M>::ShmTransmitter(const RoleAttributes& attr)
    : Transmitter<M>(attr),
      segment_(nullptr),
      channel_id_(attr.channel_id()),
      notifier_(nullptr) {
  host_id_ = common::Hash(attr.host_ip());
}

template <typename M>
ShmTransmitter<M>::~ShmTransmitter() {
  Disable();
}

template <typename M>
void ShmTransmitter<M>::Enable() {
  if (this->enabled_) {
    return;
  }

  segment_ = SegmentFactory::CreateSegment(channel_id_);
  notifier_ = NotifierFactory::CreateNotifier();
  this->enabled_ = true;
}

template <typename M>
void ShmTransmitter<M>::Disable() {
  if (this->enabled_) {
    segment_ = nullptr;
    notifier_ = nullptr;
    this->enabled_ = false;
  }
}

template <typename M>
bool ShmTransmitter<M>::Transmit(const MessagePtr& msg,
                                 const MessageInfo& msg_info) {
  return Transmit(*msg, msg_info);
}

template <typename M>
bool ShmTransmitter<M>::Transmit(const M& msg, const MessageInfo& msg_info) {
  if (!this->enabled_) {
    ADEBUG << "not enable.";
    return false;
  }

  WritableBlock wb;
  const auto byte_size = message::ByteSize(msg);
  if (byte_size < 0) {
    AERROR << "invalid message size.";
    return false;
  }
  std::size_t msg_size = static_cast<std::size_t>(byte_size);
  TransportProfileRecorder::Instance()->Record(this->attr_.channel_id(), msg_size);
  if (!segment_->AcquireBlockToWrite(msg_size, &wb)) {
    AERROR << "acquire block failed.";
    return false;
  }

  ADEBUG << "block index: " << wb.index;
  if (!message::SerializeToArray(msg, wb.buf, static_cast<int>(msg_size))) {
    AERROR << "serialize to array failed.";
    segment_->ReleaseWrittenBlock(wb);
    return false;
  }
  wb.block->set_msg_size(msg_size);

  char* msg_info_addr = reinterpret_cast<char*>(wb.buf) + msg_size;
  if (!msg_info.SerializeTo(msg_info_addr, MessageInfo::kSize)) {
    AERROR << "serialize message info failed.";
    segment_->ReleaseWrittenBlock(wb);
    return false;
  }
  wb.block->set_msg_info_size(MessageInfo::kSize);
  segment_->ReleaseWrittenBlock(wb);

  ReadableInfo readable_info(host_id_, wb.index, channel_id_);

  ADEBUG << "Writing sharedmem message: "
         << common::GlobalData::GetChannelById(channel_id_)
         << " to block: " << wb.index;
  return notifier_->Notify(readable_info);
}

template <typename M>
bool ShmTransmitter<M>::Loan(std::size_t size, LoanedMessage<M>* loaned_msg) {
  if (!this->enabled_) {
    ADEBUG << "not enable.";
    return false;
  }
  if (loaned_msg == nullptr || size == 0) {
    return false;
  }

  TransportProfileRecorder::Instance()->Record(this->attr_.channel_id(), size);

  WritableBlock wb;
  if (!segment_->AcquireBlockToWrite(size, &wb)) {
    AERROR << "acquire block failed.";
    return false;
  }

  auto attachment = std::make_shared<ShmLoanAttachment>(segment_, wb);
  loaned_msg->Reset();
  loaned_msg->SetBuffer(wb.buf, size);
  loaned_msg->SetAttachment(attachment);
  return true;
}

template <typename M>
bool ShmTransmitter<M>::Publish(LoanedMessage<M>&& loaned_msg) {
  LoanedMessage<M> moved_msg(std::move(loaned_msg));
  if (!this->enabled_) {
    ADEBUG << "not enable.";
    return false;
  }
  if (!moved_msg.valid() || moved_msg.size() == 0) {
    return false;
  }

  auto attachment =
      std::dynamic_pointer_cast<ShmLoanAttachment>(moved_msg.attachment_);
  if (attachment == nullptr) {
    return false;
  }

  this->msg_info_.set_seq_num(this->NextSeqNum());
  PerfEventCache::Instance()->AddTransportEvent(
      TransPerf::TRANSMIT_BEGIN, this->attr_.channel_id(),
      this->msg_info_.seq_num());

  attachment->wb_.block->set_msg_size(moved_msg.size());
  char* msg_info_addr =
      reinterpret_cast<char*>(attachment->wb_.buf) + moved_msg.size();
  if (!this->msg_info_.SerializeTo(msg_info_addr, MessageInfo::kSize)) {
    AERROR << "serialize message info failed.";
    return false;
  }
  attachment->wb_.block->set_msg_info_size(MessageInfo::kSize);
  segment_->ReleaseWrittenBlock(attachment->wb_);
  attachment->published_ = true;

  ReadableInfo readable_info(host_id_, attachment->wb_.index, channel_id_);
  ADEBUG << "Writing sharedmem message: "
         << common::GlobalData::GetChannelById(channel_id_)
         << " to block: " << attachment->wb_.index;
  return notifier_->Notify(readable_info);
}

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_TRANSPORT_TRANSMITTER_SHM_TRANSMITTER_H_
