#include "binding_registrars.h"

PYBIND11_MODULE(_cyber_wrapper, m) {
  apollo::cyber::RegisterRuntimeBindings(m);
  apollo::cyber::RegisterTimeBindings(m);
  apollo::cyber::RegisterNodeBindings(m);
  apollo::cyber::RegisterTimerBindings(m);
  apollo::cyber::RegisterParameterBindings(m);
  apollo::cyber::RegisterRecordBindings(m);
  apollo::cyber::RegisterTopologyBindings(m);
}
