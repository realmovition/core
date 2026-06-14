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

#include "cyber/transport/rtps/participant.h"

#include "cyber/common/global_data.h"
#include "cyber/common/log.h"
#include "cyber/proto/transport_conf.pb.h"

namespace apollo {
namespace cyber {
namespace transport {

Participant::Participant(const std::string& name, int send_port,
                         eprosima::fastrtps::ParticipantListener* listener)
    : shutdown_(false),
      name_(name),
      send_port_(send_port),
      listener_(listener),
      fastrtps_participant_(nullptr) {}

Participant::~Participant() {}

void Participant::Shutdown() {
  if (shutdown_.exchange(true)) {
    return;
  }

  std::lock_guard<std::mutex> lk(mutex_);
  if (fastrtps_participant_ != nullptr) {
    if (listener_ == nullptr) {
      eprosima::fastrtps::Domain::removeParticipant(fastrtps_participant_);
    }
    fastrtps_participant_ = nullptr;
    listener_ = nullptr;
  }
}

eprosima::fastrtps::Participant* Participant::fastrtps_participant() {
  if (shutdown_.load()) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lk(mutex_);
  if (fastrtps_participant_ != nullptr) {
    return fastrtps_participant_;
  }

  CreateFastRtpsParticipant(name_, send_port_, listener_);
  return fastrtps_participant_;
}

void Participant::CreateFastRtpsParticipant(
    const std::string& name, int send_port,
    eprosima::fastrtps::ParticipantListener* listener) {
  uint32_t domain_id = 80;

  const char* val = ::getenv("CYBER_DOMAIN_ID");
  if (val != nullptr) {
    try {
      domain_id = std::stoi(val);
    } catch (const std::exception& e) {
      AERROR << "convert domain_id error " << e.what();
      return;
    }
  }

  auto part_attr_conf = std::make_shared<proto::RtpsParticipantAttr>();
  auto& global_conf = common::GlobalData::Instance()->Config();
  if (global_conf.has_transport_conf() &&
      global_conf.transport_conf().has_participant_attr()) {
    part_attr_conf->CopyFrom(global_conf.transport_conf().participant_attr());
  }

  eprosima::fastrtps::ParticipantAttributes attr;
  attr.rtps.port.domainIDGain =
      static_cast<uint16_t>(part_attr_conf->domain_id_gain());
  attr.rtps.port.portBase = static_cast<uint16_t>(part_attr_conf->port_base());
  attr.rtps.builtin.avoid_builtin_multicast = true;
  
  attr.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
  attr.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter =
      true;
  attr.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader =
      true;
  attr.domainId = domain_id;

  /**
   * The user should set the lease_duration and the announcement_period with
   * values that differ in at least 30%. Values too close to each other may
   * cause the failure of the writer liveliness assertion in networks with high
   * latency or with lots of communication errors.
   */
  attr.rtps.builtin.discovery_config.leaseDuration.seconds = part_attr_conf->lease_duration();
  attr.rtps.builtin.discovery_config.leaseDuration_announcementperiod.seconds =
      part_attr_conf->announcement_period();

  attr.rtps.setName(name.c_str());

  std::string ip_env("127.0.0.1");
  const char* ip_val = ::getenv("CYBER_IP");
  if (ip_val != nullptr) {
    ip_env = ip_val;
    if (ip_env.empty()) {
      AERROR << "invalid CYBER_IP (an empty string)";
      return;
    }
  }
  ADEBUG << "cyber ip: " << ip_env;

  RETURN_IF(send_port <= 0);
  const auto process_id = common::GlobalData::Instance()->ProcessId();
  constexpr uint32_t kParticipantIdRange = 128;
  const uint32_t participant_id =
      ((process_id % kParticipantIdRange) * 2) + ((send_port == 11512) ? 1 : 0);
  attr.rtps.participantID = static_cast<int32_t>(participant_id);

  eprosima::fastrtps::rtps::Locator_t metatraffic_unicast_locator;
  metatraffic_unicast_locator.kind = LOCATOR_KIND_UDPv4;
  RETURN_IF(!eprosima::fastrtps::rtps::IPLocator::setIPv4(
      metatraffic_unicast_locator, ip_env));
  RETURN_IF(!eprosima::fastrtps::rtps::IPLocator::setPhysicalPort(
      metatraffic_unicast_locator, static_cast<uint16_t>(
                                       attr.rtps.port.getUnicastPort(
                                           domain_id, participant_id))));
  attr.rtps.defaultUnicastLocatorList.push_back(metatraffic_unicast_locator);
  attr.rtps.builtin.metatrafficUnicastLocatorList.push_back(
      metatraffic_unicast_locator);

  for (uint32_t id = 0; id < (kParticipantIdRange * 2); ++id) {
    eprosima::fastrtps::rtps::Locator_t peer_locator;
    peer_locator.kind = LOCATOR_KIND_UDPv4;
    RETURN_IF(!eprosima::fastrtps::rtps::IPLocator::setIPv4(peer_locator, ip_env));
    RETURN_IF(!eprosima::fastrtps::rtps::IPLocator::setPhysicalPort(
        peer_locator, static_cast<uint16_t>(
                          attr.rtps.port.getUnicastPort(domain_id, id))));
    attr.rtps.builtin.initialPeersList.push_back(peer_locator);
  }

  fastrtps_participant_ =
      eprosima::fastrtps::Domain::createParticipant(attr, listener);
  RETURN_IF_NULL(fastrtps_participant_);
  eprosima::fastrtps::Domain::registerType(fastrtps_participant_, &type_);
}

}  // namespace transport
}  // namespace cyber
}  // namespace apollo
