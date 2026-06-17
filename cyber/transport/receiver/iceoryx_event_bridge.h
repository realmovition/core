/******************************************************************************
 * Copyright 2025 WheelOS. All Rights Reserved.
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

#ifndef CYBER_TRANSPORT_RECEIVER_ICEORYX_EVENT_BRIDGE_H_
#define CYBER_TRANSPORT_RECEIVER_ICEORYX_EVENT_BRIDGE_H_

#include <functional>

#include "cyber/task/task.h"

namespace apollo {
namespace cyber {
namespace transport {

class IceoryxEventBridge {
 public:
  virtual ~IceoryxEventBridge() = default;
  virtual void Notify(const std::function<void()>& fn) = 0;
};

class AsyncIceoryxEventBridge : public IceoryxEventBridge {
 public:
  void Notify(const std::function<void()>& fn) override { cyber::Async(fn); }
};

}  // namespace transport
}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_TRANSPORT_RECEIVER_ICEORYX_EVENT_BRIDGE_H_
