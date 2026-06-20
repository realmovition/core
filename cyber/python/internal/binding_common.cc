#include "binding_common.h"

#include <chrono>
#include <thread>

#include "cyber/common/log.h"

namespace apollo {
namespace cyber {

std::string BytesToString(const py::bytes& bytes) { return bytes; }

py::bytes StringToBytes(const std::string& value) { return py::bytes(value); }

void SleepForDiscovery(uint8_t sleep_s) {
  if (sleep_s == 0) {
    return;
  }

  py::gil_scoped_release release;
  std::this_thread::sleep_for(std::chrono::seconds(sleep_s));
}

void DiscardUnhandledPythonError(py::error_already_set& err,
                                 const char* context) {
  err.discard_as_unraisable(context);
  AERROR << "unhandled exception in " << context;
}

[[noreturn]] void ThrowClosed(const char* resource_name) {
  throw std::runtime_error(std::string(resource_name) + " has been closed");
}

}  // namespace cyber
}  // namespace apollo
