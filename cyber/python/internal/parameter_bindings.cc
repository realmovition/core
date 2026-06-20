#include "binding_registrars.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cyber/parameter/parameter.h"
#include "cyber/parameter/parameter_client.h"
#include "cyber/parameter/parameter_server.h"
#include "node_bindings.h"

namespace apollo {
namespace cyber {
namespace {

class ParameterClientBinding {
 public:
  ParameterClientBinding(std::shared_ptr<NodeBinding> node_binding,
                         const std::string& service_node_name)
      : node_(node_binding->shared_node()),
        parameter_client_(
            std::make_unique<ParameterClient>(node_, service_node_name)) {}

  void close() {
    parameter_client_.reset();
    node_.reset();
  }

  bool set_parameter(const Parameter& parameter) {
    EnsureOpen(parameter_client_, "parameter client");
    return parameter_client_->SetParameter(parameter);
  }

  Parameter get_parameter(const std::string& parameter_name) {
    EnsureOpen(parameter_client_, "parameter client");
    Parameter parameter;
    if (!parameter_client_->GetParameter(parameter_name, &parameter)) {
      return Parameter();
    }
    return Parameter(parameter);
  }

  std::vector<Parameter> get_parameter_list() {
    EnsureOpen(parameter_client_, "parameter client");
    std::vector<Parameter> parameters;
    parameter_client_->ListParameters(&parameters);
    return parameters;
  }

 private:
  std::shared_ptr<Node> node_;
  std::unique_ptr<ParameterClient> parameter_client_;
};

class ParameterServerBinding {
 public:
  explicit ParameterServerBinding(std::shared_ptr<NodeBinding> node_binding)
      : node_(node_binding->shared_node()),
        parameter_server_(std::make_unique<ParameterServer>(node_)) {}

  void close() {
    parameter_server_.reset();
    node_.reset();
  }

  void set_parameter(const Parameter& parameter) {
    EnsureOpen(parameter_server_, "parameter server");
    parameter_server_->SetParameter(parameter);
  }

  Parameter get_parameter(const std::string& parameter_name) {
    EnsureOpen(parameter_server_, "parameter server");
    Parameter parameter;
    if (!parameter_server_->GetParameter(parameter_name, &parameter)) {
      return Parameter();
    }
    return Parameter(parameter);
  }

  std::vector<Parameter> get_parameter_list() {
    EnsureOpen(parameter_server_, "parameter server");
    std::vector<Parameter> parameters;
    parameter_server_->ListParameters(&parameters);
    return parameters;
  }

 private:
  std::shared_ptr<Node> node_;
  std::unique_ptr<ParameterServer> parameter_server_;
};

}  // namespace

void RegisterParameterBindings(py::module_& m) {
  py::class_<apollo::cyber::Parameter>(m, "Parameter")
      .def(py::init<>())
      .def(py::init<const std::string&>())
      .def(py::init<const std::string&, bool>())
      .def(py::init<const std::string&, int64_t>())
      .def(py::init<const std::string&, double>())
      .def(py::init<const std::string&, const std::string&>())
      .def(py::init([](const std::string& name, const py::bytes& message,
                       const std::string& full_name,
                       const py::bytes& proto_desc) {
             const std::string message_data = message;
             const std::string proto_desc_data = proto_desc;
             return apollo::cyber::Parameter(name, message_data, full_name,
                                             proto_desc_data);
           }))
      .def("type", [](const apollo::cyber::Parameter& parameter) {
        return static_cast<uint32_t>(parameter.Type());
      })
      .def("type_name", &apollo::cyber::Parameter::TypeName)
      .def("descriptor", [](const apollo::cyber::Parameter& parameter) {
        return py::bytes(parameter.Descriptor());
      })
      .def("name", &apollo::cyber::Parameter::Name)
      .def("as_bool", &apollo::cyber::Parameter::AsBool)
      .def("as_int64", &apollo::cyber::Parameter::AsInt64)
      .def("as_double", &apollo::cyber::Parameter::AsDouble)
      .def("as_string", &apollo::cyber::Parameter::AsString)
      .def("debug_string", &apollo::cyber::Parameter::DebugString);

  py::class_<ParameterClientBinding, std::shared_ptr<ParameterClientBinding>>(
      m, "ParameterClient")
      .def(py::init<std::shared_ptr<NodeBinding>, const std::string&>())
      .def("close", &ParameterClientBinding::close)
      .def("set_parameter", &ParameterClientBinding::set_parameter)
      .def("get_parameter", &ParameterClientBinding::get_parameter)
      .def("get_parameter_list", &ParameterClientBinding::get_parameter_list);

  py::class_<ParameterServerBinding, std::shared_ptr<ParameterServerBinding>>(
      m, "ParameterServer")
      .def(py::init<std::shared_ptr<NodeBinding>>())
      .def("close", &ParameterServerBinding::close)
      .def("set_parameter", &ParameterServerBinding::set_parameter)
      .def("get_parameter", &ParameterServerBinding::get_parameter)
      .def("get_parameter_list", &ParameterServerBinding::get_parameter_list);
}

}  // namespace cyber
}  // namespace apollo
