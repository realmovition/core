#ifndef CYBER_PYTHON_INTERNAL_MODULE_STATE_H_
#define CYBER_PYTHON_INTERNAL_MODULE_STATE_H_

#include <string>

namespace apollo {
namespace cyber {

bool CallbacksSuppressed();
bool InitBinding(const std::string& module_name);
void ShutdownBinding();
void ShutdownForPythonExitBinding();
bool OkBinding();
bool IsShutdownBinding();
void WaitForShutdownBinding();

}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_PYTHON_INTERNAL_MODULE_STATE_H_
