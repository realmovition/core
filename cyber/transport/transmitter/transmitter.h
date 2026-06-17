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

#ifndef CYBER_TRANSPORT_TRANSMITTER_TRANSMITTER_H_
#define CYBER_TRANSPORT_TRANSMITTER_TRANSMITTER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "cyber/event/perf_event_cache.h"
#include "cyber/transport/common/endpoint.h"
#include "cyber/transport/message/message_info.h"

namespace apollo {
namespace cyber {
namespace transport {

using apollo::cyber::event::PerfEventCache;
using apollo::cyber::event::TransPerf;

template <typename M>
class ShmTransmitter;
template <typename M>
class HybridTransmitter;

class LoanAttachment {
 public:
  virtual ~LoanAttachment() = default;
};

template <typename M>
class LoanedMessage {
 public:
  LoanedMessage() = default;
  LoanedMessage(const LoanedMessage&) = delete;
  LoanedMessage& operator=(const LoanedMessage&) = delete;
  LoanedMessage(LoanedMessage&& other) noexcept
      : attachment_(std::move(other.attachment_)),
        data_(other.data_),
        capacity_(other.capacity_),
        size_(other.size_) {
    other.Reset();
  }
  LoanedMessage& operator=(LoanedMessage&& other) noexcept {
    if (this != &other) {
      attachment_ = std::move(other.attachment_);
      data_ = other.data_;
      capacity_ = other.capacity_;
      size_ = other.size_;
      other.Reset();
    }
    return *this;
  }

  bool valid() const { return data_ != nullptr && capacity_ > 0; }
  uint8_t* data() { return data_; }
  const uint8_t* data() const { return data_; }
  std::size_t capacity() const { return capacity_; }
  std::size_t size() const { return size_; }
  bool set_size(std::size_t size) {
    if (size > capacity_) {
      return false;
    }
    size_ = size;
    return true;
  }

 private:
  template <typename>
  friend class Transmitter;
  template <typename>
  friend class ShmTransmitter;
  template <typename>
  friend class IceoryxTransmitter;
  template <typename>
  friend class HybridTransmitter;

  void Reset() {
    data_ = nullptr;
    capacity_ = 0;
    size_ = 0;
    attachment_.reset();
  }

  void SetBuffer(uint8_t* data, std::size_t capacity) {
    data_ = data;
    capacity_ = capacity;
    size_ = 0;
  }

  void SetAttachment(const std::shared_ptr<LoanAttachment>& attachment) {
    attachment_ = attachment;
  }

  std::shared_ptr<LoanAttachment> attachment_;
  uint8_t* data_ = nullptr;
  std::size_t capacity_ = 0;
  std::size_t size_ = 0;
};

template <typename M>
class Transmitter : public Endpoint {
 public:
  using MessagePtr = std::shared_ptr<M>;

  explicit Transmitter(const RoleAttributes& attr);
  virtual ~Transmitter();

  virtual void Enable() = 0;
  virtual void Disable() = 0;

  virtual void Enable(const RoleAttributes& opposite_attr);
  virtual void Disable(const RoleAttributes& opposite_attr);

  virtual bool Transmit(const MessagePtr& msg);
  virtual bool Transmit(const MessagePtr& msg, const MessageInfo& msg_info) = 0;
  virtual bool IsLoanSupported() const { return false; }
  virtual bool Loan(std::size_t size, LoanedMessage<M>* loaned_msg) {
    (void)size;
    (void)loaned_msg;
    return false;
  }
  virtual bool Publish(LoanedMessage<M>&& loaned_msg) {
    (void)loaned_msg;
    return false;
  }

  uint64_t NextSeqNum() { return ++seq_num_; }

  uint64_t seq_num() const { return seq_num_; }

 protected:
  uint64_t seq_num_;
  MessageInfo msg_info_;
};

template <typename M>
Transmitter<M>::Transmitter(const RoleAttributes& attr)
    : Endpoint(attr), seq_num_(0) {
  msg_info_.set_sender_id(this->id_);
  msg_info_.set_seq_num(this->seq_num_);
}

template <typename M>
Transmitter<M>::~Transmitter() {}

template <typename M>
bool Transmitter<M>::Transmit(const MessagePtr& msg) {
  msg_info_.set_seq_num(NextSeqNum());
  PerfEventCache::Instance()->AddTransportEvent(
      TransPerf::TRANSMIT_BEGIN, attr_.channel_id(), msg_info_.seq_num());
  return Transmit(msg, msg_info_);
}

template <typename M>
void Transmitter<M>::Enable(const RoleAttributes& opposite_attr) {
  (void)opposite_attr;
  Enable();
}

template <typename M>
void Transmitter<M>::Disable(const RoleAttributes& opposite_attr) {
  (void)opposite_attr;
  Disable();
}

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_TRANSPORT_TRANSMITTER_TRANSMITTER_H_
