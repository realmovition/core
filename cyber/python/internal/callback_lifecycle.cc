#include "callback_lifecycle.h"

#include <thread>
#include <unordered_map>
#include <utility>

#include "module_state.h"

namespace apollo {
namespace cyber {
namespace {

thread_local std::unordered_map<const CallbackLifecycle*, size_t>
    g_active_callbacks;
std::atomic<size_t> g_total_callbacks{0};
std::mutex g_total_callbacks_mutex;
std::condition_variable g_total_callbacks_cv;
std::atomic<size_t> g_detached_close_tasks{0};
std::mutex g_detached_close_tasks_mutex;
std::condition_variable g_detached_close_tasks_cv;

}  // namespace

bool InPythonCallbackOnCurrentThread() { return !g_active_callbacks.empty(); }

void WaitForPythonCallbacksToDrain() {
  std::unique_lock<std::mutex> lock(g_total_callbacks_mutex);
  g_total_callbacks_cv.wait(lock, [] {
    return g_total_callbacks.load(std::memory_order_acquire) == 0;
  });
}

void RunDetachedCloseTask(std::function<void()> task) {
  // Callback-owned resources that cannot finalize on the callback thread hand
  // their close tail to this helper so shutdown can wait for those tails too.
  g_detached_close_tasks.fetch_add(1, std::memory_order_acq_rel);
  std::thread([task = std::move(task)]() mutable {
    task();
    if (g_detached_close_tasks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      std::lock_guard<std::mutex> lock(g_detached_close_tasks_mutex);
      g_detached_close_tasks_cv.notify_all();
    }
  }).detach();
}

void WaitForDetachedCloseTasksToDrain() {
  std::unique_lock<std::mutex> lock(g_detached_close_tasks_mutex);
  g_detached_close_tasks_cv.wait(lock, [] {
    return g_detached_close_tasks.load(std::memory_order_acquire) == 0;
  });
}

CallbackToken::CallbackToken(CallbackToken&& other) noexcept
    : lifecycle_(std::exchange(other.lifecycle_, nullptr)) {}

CallbackToken& CallbackToken::operator=(CallbackToken&& other) noexcept {
  if (this != &other) {
    Reset();
    lifecycle_ = std::exchange(other.lifecycle_, nullptr);
  }
  return *this;
}

CallbackToken::~CallbackToken() { Reset(); }

void CallbackToken::Reset() {
  if (lifecycle_ != nullptr) {
    auto active_it = g_active_callbacks.find(lifecycle_);
    if (active_it != g_active_callbacks.end()) {
      if (--active_it->second == 0) {
        g_active_callbacks.erase(active_it);
      }
    }
    if (g_total_callbacks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      std::lock_guard<std::mutex> lock(g_total_callbacks_mutex);
      g_total_callbacks_cv.notify_all();
    }
    lifecycle_->Release();
    lifecycle_ = nullptr;
  }
}

bool CallbackLifecycle::BeginClose() {
  bool expected = false;
  return closed_.compare_exchange_strong(expected, true);
}

bool CallbackLifecycle::IsClosed() const {
  return closed_.load(std::memory_order_acquire);
}

bool CallbackLifecycle::IsActiveOnCurrentThread() const {
  const auto active_it = g_active_callbacks.find(this);
  return active_it != g_active_callbacks.end() && active_it->second != 0;
}

void CallbackLifecycle::SetCallback(py::function callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  callback_ = std::move(callback);
}

bool CallbackLifecycle::Acquire(py::function* callback, CallbackToken* token) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  if (IsClosed() || CallbacksSuppressed() || !callback_) {
    return false;
  }

  ++in_flight_callbacks_;
  *callback = callback_;
  *token = CallbackToken(this);
  ++g_active_callbacks[this];
  g_total_callbacks.fetch_add(1, std::memory_order_acq_rel);
  return true;
}

void CallbackLifecycle::WaitForDrain() {
  std::unique_lock<std::mutex> lock(callback_mutex_);
  callback_cv_.wait(lock, [this] { return in_flight_callbacks_ == 0; });
}

void CallbackLifecycle::ClearCallback() {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  callback_ = py::function();
}

void CallbackLifecycle::Release() {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  --in_flight_callbacks_;
  if (closed_.load(std::memory_order_acquire) && in_flight_callbacks_ == 0) {
    callback_cv_.notify_all();
  }
}

}  // namespace cyber
}  // namespace apollo
