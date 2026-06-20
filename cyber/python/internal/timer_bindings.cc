#include "binding_registrars.h"

#include <atomic>
#include <memory>
#include <thread>
#include <utility>

#include "cyber/common/log.h"
#include "callback_lifecycle.h"
#include "module_state.h"
#include "cyber/timer/timer.h"

namespace apollo {
namespace cyber {
namespace {

void WaitForDrainWithOptionalGilRelease(CallbackLifecycle* lifecycle) {
  if (PyGILState_Check()) {
    py::gil_scoped_release release;
    lifecycle->WaitForDrain();
    return;
  }
  lifecycle->WaitForDrain();
}

class TimerBinding : public std::enable_shared_from_this<TimerBinding> {
 public:
  TimerBinding() : timer_(std::make_shared<Timer>()) {}

  TimerBinding(uint32_t period, py::function callback, bool oneshot)
      : TimerBinding() {
    set_option(period, std::move(callback), oneshot);
  }

  ~TimerBinding() {
    try {
      close();
    } catch (...) {
    }
  }

  void start() {
    EnsureOpen(timer_, "timer");
    timer_->Start();
  }

  void stop() {
    if (timer_ != nullptr) {
      py::gil_scoped_release release;
      timer_->Stop();
    }
  }

  void set_option(uint32_t period, py::function callback, bool oneshot) {
    EnsureOpen(timer_, "timer");
    lifecycle_.SetCallback(std::move(callback));

    TimerOption option;
    option.period = period;
    option.oneshot = oneshot;
    option.callback = [this]() { Dispatch(); };
    timer_->SetTimerOption(option);
  }

  void close() {
    if (!lifecycle_.BeginClose()) {
      return;
    }

    if (lifecycle_.IsActiveOnCurrentThread()) {
      RunDetachedCloseTask(
          [self = shared_from_this()]() mutable { self->FinishClose(); });
      return;
    }

    FinishClose();
  }

 private:
  void FinishClose() {
    if (timer_ != nullptr) {
      if (PyGILState_Check()) {
        py::gil_scoped_release release;
        timer_->Stop();
      } else {
        timer_->Stop();
      }
    }
    timer_.reset();
    WaitForDrainWithOptionalGilRelease(&lifecycle_);
    py::gil_scoped_acquire acquire;
    lifecycle_.ClearCallback();
  }

  void Dispatch() {
    if (lifecycle_.IsClosed() || CallbacksSuppressed()) {
      return;
    }

    py::gil_scoped_acquire acquire;

    py::function callback;
    CallbackToken token;
    if (!lifecycle_.Acquire(&callback, &token)) {
      return;
    }

    try {
      callback();
    } catch (py::error_already_set& err) {
      DiscardUnhandledPythonError(err, "pycyber timer callback");
    }
  }

  std::shared_ptr<Timer> timer_;
  CallbackLifecycle lifecycle_;
};

}  // namespace

void RegisterTimerBindings(py::module_& m) {
  py::class_<TimerBinding, std::shared_ptr<TimerBinding>>(m, "Timer")
      .def(py::init<>())
      .def(py::init<uint32_t, py::function, bool>(), py::arg("period"),
           py::arg("callback"), py::arg("oneshot"))
      .def("start", &TimerBinding::start)
      .def("stop", &TimerBinding::stop)
      .def("set_option", &TimerBinding::set_option, py::arg("period"),
           py::arg("callback"), py::arg("oneshot"))
      .def("close", &TimerBinding::close);
}

}  // namespace cyber
}  // namespace apollo
