#include "binding_registrars.h"

#include "module_state.h"

namespace apollo {
namespace cyber {

void RegisterRuntimeBindings(py::module_& m) {
  m.def("init", &InitBinding, py::arg("module_name") = "cyber_py");
  m.def("ok", &OkBinding);
  m.def("shutdown", &ShutdownBinding);
  m.def("_shutdown_for_python_exit", &ShutdownForPythonExitBinding);
  m.def("is_shutdown", &IsShutdownBinding);
  m.def("wait_for_shutdown", &WaitForShutdownBinding,
        py::call_guard<py::gil_scoped_release>());

  m.def("py_init", &InitBinding, py::arg("module_name") = "cyber_py");
  m.def("py_ok", &OkBinding);
  m.def("py_shutdown", &ShutdownBinding);
  m.def("py_is_shutdown", &IsShutdownBinding);
  m.def("py_waitforshutdown", &WaitForShutdownBinding,
        py::call_guard<py::gil_scoped_release>());
}

}  // namespace cyber
}  // namespace apollo
