#ifndef CYBER_PYTHON_INTERNAL_BINDING_REGISTRARS_H_
#define CYBER_PYTHON_INTERNAL_BINDING_REGISTRARS_H_

#include "binding_common.h"

namespace apollo {
namespace cyber {

void RegisterRuntimeBindings(py::module_& m);
void RegisterTimeBindings(py::module_& m);
void RegisterNodeBindings(py::module_& m);
void RegisterTimerBindings(py::module_& m);
void RegisterParameterBindings(py::module_& m);
void RegisterRecordBindings(py::module_& m);
void RegisterTopologyBindings(py::module_& m);

}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_PYTHON_INTERNAL_BINDING_REGISTRARS_H_
