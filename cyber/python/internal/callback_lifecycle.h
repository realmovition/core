#ifndef CYBER_PYTHON_INTERNAL_CALLBACK_LIFECYCLE_H_
#define CYBER_PYTHON_INTERNAL_CALLBACK_LIFECYCLE_H_

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>

#include "binding_common.h"

namespace apollo {
namespace cyber {

bool InPythonCallbackOnCurrentThread();
void WaitForPythonCallbacksToDrain();
void RunDetachedCloseTask(std::function<void()> task);
void WaitForDetachedCloseTasksToDrain();

class CallbackLifecycle;

class CallbackToken {
 public:
  CallbackToken() = default;
  CallbackToken(const CallbackToken&) = delete;
  CallbackToken& operator=(const CallbackToken&) = delete;
  CallbackToken(CallbackToken&& other) noexcept;
  CallbackToken& operator=(CallbackToken&& other) noexcept;
  ~CallbackToken();

  void Reset();
  explicit operator bool() const { return lifecycle_ != nullptr; }

 private:
  friend class CallbackLifecycle;
  explicit CallbackToken(CallbackLifecycle* lifecycle) : lifecycle_(lifecycle) {}

  CallbackLifecycle* lifecycle_ = nullptr;
};

class CallbackLifecycle {
 public:
  CallbackLifecycle() = default;
  CallbackLifecycle(const CallbackLifecycle&) = delete;
  CallbackLifecycle& operator=(const CallbackLifecycle&) = delete;

  bool BeginClose();
  bool IsClosed() const;
  bool IsActiveOnCurrentThread() const;
  void SetCallback(py::function callback);
  bool Acquire(py::function* callback, CallbackToken* token);
  void WaitForDrain();
  void ClearCallback();

 private:
  friend class CallbackToken;
  void Release();

  std::atomic<bool> closed_{false};
  py::function callback_;
  std::mutex callback_mutex_;
  std::condition_variable callback_cv_;
  size_t in_flight_callbacks_ = 0;
};

}  // namespace cyber
}  // namespace apollo

#endif  // CYBER_PYTHON_INTERNAL_CALLBACK_LIFECYCLE_H_
