#pragma once
#include "transport.h"
#include <mutex>
#include <utility>
#ifdef USE_HOST
#include <thread>
#include <condition_variable>
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace glaze {

// Single-slot mailbox. All queue policy and callbacks stay on the ESPHome loop.
class NetworkWorker {
 public:
  bool start(Request job) {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef USE_HOST
    if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
#else
    if (!thread_ && xTaskCreate([](void *self) { static_cast<NetworkWorker *>(self)->loop(); },
                               "glaze_http", 16384, this, 1, &thread_) != pdPASS) return false;
#endif
    job_ = std::move(job);
#ifdef USE_HOST
    wake_.notify_one();
#else
    xTaskNotifyGive(thread_);
#endif
    return true;
  }

  std::optional<Reply> take() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(result_, std::nullopt);
  }
#ifdef USE_HOST
  ~NetworkWorker() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    wake_.notify_one();
    if (thread_.joinable()) thread_.join();
  }
#endif

 private:
  std::mutex mutex_;
  std::optional<Request> job_;
  std::optional<Reply> result_;
#ifdef USE_HOST
  std::thread thread_;
  std::condition_variable wake_;
  bool stopping_ = false;
#else
  TaskHandle_t thread_ = nullptr;
#endif
  void loop() {
    Transport transport;
    for (;;) {
      Request job;
#ifdef USE_HOST
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return stopping_ || job_.has_value(); });
        if (stopping_) return;
#else
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      {
        std::lock_guard<std::mutex> lock(mutex_);
#endif
        job = std::move(*job_);
        job_.reset();
      }
      auto result = transport.perform(job);
      std::lock_guard<std::mutex> lock(mutex_);
      result_ = std::move(result);
    }
  }
};
}  // namespace glaze
