#ifndef CYBER_PYTHON_INTERNAL_BINDING_COMMON_H_
#define CYBER_PYTHON_INTERNAL_BINDING_COMMON_H_

#include <Python.h>

#include <cstdint>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace apollo {
namespace cyber {

std::string BytesToString(const py::bytes& bytes);
py::bytes StringToBytes(const std::string& value);
void SleepForDiscovery(uint8_t sleep_s);
void DiscardUnhandledPythonError(py::error_already_set& err,
                                 const char* context);
[[noreturn]] void ThrowClosed(const char* resource_name);

template <typename Pointer>
void EnsureOpen(const Pointer& pointer, const char* resource_name) {
  if (pointer == nullptr) {
    ThrowClosed(resource_name);
  }
}

}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_PYTHON_INTERNAL_BINDING_COMMON_H_
