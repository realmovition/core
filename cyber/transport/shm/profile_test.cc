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

#include "cyber/transport/shm/profile.h"

#include "gtest/gtest.h"

namespace apollo {
namespace cyber {
namespace transport {

TEST(TransportProfileRecorderTest, generate_toml) {
  const auto channel_id = common::GlobalData::RegisterChannel("profile_test");
  TransportProfileRecorder::Instance()->Record(channel_id, 128);
  TransportProfileRecorder::Instance()->Record(channel_id, 256);
  const auto toml = TransportProfileRecorder::Instance()->GenerateToml();
  EXPECT_NE(toml.find("profile_test"), std::string::npos);
  EXPECT_NE(toml.find("max_payload_size = 256"), std::string::npos);
}

}  // namespace transport
}  // namespace cyber
}  // namespace apollo
