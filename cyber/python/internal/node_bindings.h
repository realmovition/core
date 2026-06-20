#ifndef CYBER_PYTHON_INTERNAL_NODE_BINDINGS_H_
#define CYBER_PYTHON_INTERNAL_NODE_BINDINGS_H_

#include <memory>
#include <string>

#include "cyber/node/node.h"
#include "binding_common.h"

namespace apollo {
namespace cyber {

class WriterBinding;
class ReaderBinding;
class ClientBinding;
class ServiceBinding;

class NodeBinding {
 public:
  explicit NodeBinding(const std::string& node_name);

  void close();
  void register_message(const py::bytes& descriptor);
  std::shared_ptr<WriterBinding> create_writer(const std::string& channel_name,
                                               const std::string& data_type,
                                               uint32_t qos_depth);
  std::shared_ptr<ReaderBinding> create_reader(const std::string& channel_name,
                                               bool raw_data,
                                               py::function callback);
  std::shared_ptr<ClientBinding> create_client(const std::string& service_name,
                                               const std::string& request_type);
  std::shared_ptr<ServiceBinding> create_service(
      const std::string& service_name, const std::string& response_type,
      py::function callback);
  std::shared_ptr<Node> shared_node() const;

 private:
  void EnsureOpen() const;

  std::string node_name_;
  std::shared_ptr<Node> node_;
};

void RegisterNodeBindings(py::module_& m);

}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_PYTHON_INTERNAL_NODE_BINDINGS_H_
