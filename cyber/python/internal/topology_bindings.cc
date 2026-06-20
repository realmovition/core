#include "binding_registrars.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "cyber/common/log.h"
#include "cyber/message/protobuf_factory.h"
#include "cyber/proto/role_attributes.pb.h"
#include "cyber/service_discovery/topology_manager.h"

namespace apollo {
namespace cyber {
namespace {

class ChannelUtilsBinding {
 public:
  static std::string get_debugstring_rawmsgdata(const std::string& msg_type,
                                                const py::bytes& rawmsg_data) {
    if (msg_type.empty()) {
      AERROR << "parse rawmessage the msg_type is null";
      return "";
    }

    const std::string raw_data = BytesToString(rawmsg_data);
    if (raw_data.empty()) {
      AERROR << "parse rawmessage the rawmsgdata is null";
      return "";
    }

    std::unique_ptr<google::protobuf::Message> message(
        message::ProtobufFactory::Instance()->GenerateMessageByType(msg_type));
    if (message == nullptr) {
      AERROR << "raw message class is null";
      return "";
    }

    if (!message->ParseFromString(raw_data)) {
      AERROR << "Cannot parse the msg [ " << msg_type << " ]";
      return "";
    }

    return message->DebugString();
  }

  static std::string get_msgtype(const std::string& channel_name,
                                 uint8_t sleep_s = 0) {
    if (channel_name.empty()) {
      AERROR << "channel_name is null";
      return "";
    }

    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);
    std::string message_type;
    topology->channel_manager()->GetMsgType(channel_name, &message_type);
    return message_type;
  }

  static std::vector<std::string> get_channels(uint8_t sleep_s = 2) {
    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);
    std::vector<std::string> channels;
    topology->channel_manager()->GetChannelNames(&channels);
    return channels;
  }

  static py::dict get_channels_info(uint8_t sleep_s = 2) {
    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);

    py::dict roles_info;
    std::vector<proto::RoleAttributes> roles;
    auto channel_manager = topology->channel_manager();

    auto append_roles = [&](const std::vector<proto::RoleAttributes>& attrs) {
      for (const auto& attr : attrs) {
        std::string serialized;
        attr.SerializeToString(&serialized);
        py::str channel_name(attr.channel_name());
        if (!roles_info.contains(channel_name)) {
          roles_info[channel_name] = py::list();
        }
        roles_info[channel_name].cast<py::list>().append(
            StringToBytes(serialized));
      }
    };

    channel_manager->GetWriters(&roles);
    append_roles(roles);
    roles.clear();
    channel_manager->GetReaders(&roles);
    append_roles(roles);
    return roles_info;
  }
};

class NodeUtilsBinding {
 public:
  static std::vector<std::string> get_nodes(uint8_t sleep_s = 2) {
    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);
    std::vector<std::string> node_names;
    std::vector<proto::RoleAttributes> nodes;
    topology->node_manager()->GetNodes(&nodes);
    if (nodes.empty()) {
      AERROR << "no node found.";
      return node_names;
    }

    std::sort(nodes.begin(), nodes.end(),
              [](const proto::RoleAttributes& lhs,
                 const proto::RoleAttributes& rhs) {
                return lhs.node_name().compare(rhs.node_name()) <= 0;
              });
    for (const auto& node : nodes) {
      node_names.emplace_back(node.node_name());
    }
    return node_names;
  }

  static py::bytes get_node_attr(const std::string& node_name,
                                 uint8_t sleep_s = 2) {
    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);

    if (!topology->node_manager()->HasNode(node_name)) {
      AERROR << "no node named: " << node_name;
      return py::bytes("");
    }

    std::vector<proto::RoleAttributes> nodes;
    topology->node_manager()->GetNodes(&nodes);
    for (const auto& node_attr : nodes) {
      if (node_attr.node_name() == node_name) {
        std::string data;
        node_attr.SerializeToString(&data);
        return StringToBytes(data);
      }
    }
    return py::bytes("");
  }

  static std::vector<std::string> get_readersofnode(const std::string& node_name,
                                                    uint8_t sleep_s = 2) {
    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);
    std::vector<std::string> reader_channels;
    if (!topology->node_manager()->HasNode(node_name)) {
      AERROR << "no node named: " << node_name;
      return reader_channels;
    }

    std::vector<proto::RoleAttributes> readers;
    topology->channel_manager()->GetReadersOfNode(node_name, &readers);
    for (const auto& reader : readers) {
      if (reader.channel_name() == "param_event") {
        continue;
      }
      reader_channels.emplace_back(reader.channel_name());
    }
    return reader_channels;
  }

  static std::vector<std::string> get_writersofnode(const std::string& node_name,
                                                    uint8_t sleep_s = 2) {
    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);
    std::vector<std::string> writer_channels;
    if (!topology->node_manager()->HasNode(node_name)) {
      AERROR << "no node named: " << node_name;
      return writer_channels;
    }

    std::vector<proto::RoleAttributes> writers;
    topology->channel_manager()->GetWritersOfNode(node_name, &writers);
    for (const auto& writer : writers) {
      if (writer.channel_name() == "param_event") {
        continue;
      }
      writer_channels.emplace_back(writer.channel_name());
    }
    return writer_channels;
  }
};

class ServiceUtilsBinding {
 public:
  static std::vector<std::string> get_services(uint8_t sleep_s = 2) {
    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);
    std::vector<std::string> service_names;
    std::vector<proto::RoleAttributes> services;
    topology->service_manager()->GetServers(&services);
    if (services.empty()) {
      AERROR << "no service found.";
      return service_names;
    }

    std::sort(services.begin(), services.end(),
              [](const proto::RoleAttributes& lhs,
                 const proto::RoleAttributes& rhs) {
                return lhs.service_name().compare(rhs.service_name()) <= 0;
              });
    for (const auto& service : services) {
      service_names.emplace_back(service.service_name());
    }
    return service_names;
  }

  static py::bytes get_service_attr(const std::string& service_name,
                                    uint8_t sleep_s = 2) {
    auto topology = service_discovery::TopologyManager::Instance();
    SleepForDiscovery(sleep_s);

    if (!topology->service_manager()->HasService(service_name)) {
      AERROR << "no service: " << service_name;
      return py::bytes("");
    }

    std::vector<proto::RoleAttributes> services;
    topology->service_manager()->GetServers(&services);
    for (const auto& service_attr : services) {
      if (service_attr.service_name() == service_name) {
        std::string data;
        service_attr.SerializeToString(&data);
        return StringToBytes(data);
      }
    }
    return py::bytes("");
  }
};

}  // namespace

void RegisterTopologyBindings(py::module_& m) {
  py::class_<ChannelUtilsBinding>(m, "ChannelUtils")
      .def_static("get_debugstring_rawmsgdata",
                  &ChannelUtilsBinding::get_debugstring_rawmsgdata)
      .def_static("get_msgtype", &ChannelUtilsBinding::get_msgtype,
                  py::arg("channel_name"), py::arg("sleep_s") = 2)
      .def_static("get_channels", &ChannelUtilsBinding::get_channels,
                  py::arg("sleep_s") = 2)
      .def_static("get_channels_info", &ChannelUtilsBinding::get_channels_info,
                  py::arg("sleep_s") = 2);

  py::class_<NodeUtilsBinding>(m, "NodeUtils")
      .def_static("get_nodes", &NodeUtilsBinding::get_nodes,
                  py::arg("sleep_s") = 2)
      .def_static("get_node_attr", &NodeUtilsBinding::get_node_attr,
                  py::arg("node_name"), py::arg("sleep_s") = 2)
      .def_static("get_readersofnode", &NodeUtilsBinding::get_readersofnode,
                  py::arg("node_name"), py::arg("sleep_s") = 2)
      .def_static("get_writersofnode", &NodeUtilsBinding::get_writersofnode,
                  py::arg("node_name"), py::arg("sleep_s") = 2);

  py::class_<ServiceUtilsBinding>(m, "ServiceUtils")
      .def_static("get_services", &ServiceUtilsBinding::get_services,
                  py::arg("sleep_s") = 2)
      .def_static("get_service_attr", &ServiceUtilsBinding::get_service_attr,
                  py::arg("service_name"), py::arg("sleep_s") = 2);
}

}  // namespace cyber
}  // namespace apollo
