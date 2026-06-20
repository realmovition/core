#include "binding_registrars.h"

#include "cyber/time/duration.h"
#include "cyber/time/rate.h"
#include "cyber/time/time.h"

namespace apollo {
namespace cyber {

void RegisterTimeBindings(py::module_& m) {
  py::class_<apollo::cyber::Duration>(m, "Duration")
      .def(py::init<int64_t>())
      .def(py::init<double>())
      .def("sleep", &apollo::cyber::Duration::Sleep,
           py::call_guard<py::gil_scoped_release>())
      .def("to_sec", &apollo::cyber::Duration::ToSecond)
      .def("to_nsec", &apollo::cyber::Duration::ToNanosecond)
      .def("iszero", &apollo::cyber::Duration::IsZero);

  py::class_<apollo::cyber::Time>(m, "Time")
      .def(py::init<uint64_t>())
      .def(py::init<double>())
      .def_static("now", &apollo::cyber::Time::Now)
      .def_static("mono_time", &apollo::cyber::Time::MonoTime)
      .def("sleep_until",
           [](const apollo::cyber::Time&, uint64_t nanoseconds) {
             apollo::cyber::Time::SleepUntil(apollo::cyber::Time(nanoseconds));
           },
           py::call_guard<py::gil_scoped_release>())
      .def("to_sec", &apollo::cyber::Time::ToSecond)
      .def("to_nsec", &apollo::cyber::Time::ToNanosecond)
      .def("iszero", &apollo::cyber::Time::IsZero);

  py::class_<apollo::cyber::Rate>(m, "Rate")
      .def(py::init<uint64_t>())
      .def("sleep", &apollo::cyber::Rate::Sleep,
           py::call_guard<py::gil_scoped_release>())
      .def("reset", &apollo::cyber::Rate::Reset)
      .def("get_cycle_time",
           [](const apollo::cyber::Rate& rate) {
             return rate.CycleTime().ToNanosecond();
           })
      .def("get_expected_cycle_time",
           [](const apollo::cyber::Rate& rate) {
             return rate.ExpectedCycleTime().ToNanosecond();
           });
}

}  // namespace cyber
}  // namespace apollo
