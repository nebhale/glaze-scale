#pragma once

#include "network_worker.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <vector>

namespace glaze {

inline std::string url_part(const std::string &value) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (unsigned char c : value) {
    if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == '~') result += c;
    else {
      result += '%';
      result += hex[c >> 4];
      result += hex[c & 15];
    }
  }
  return result;
}

enum class ServiceStatus { Unchecked, Available, Unavailable };

class Manager {
 public:
  ServiceStatus status = ServiceStatus::Unchecked;

  using Done = std::function<void(const Reply &)>;

  void network_changed() {
    if (connection_) connection_ = std::make_shared<Connection>(*connection_);
    ++generation_;
    if (status == ServiceStatus::Available) status = ServiceStatus::Unchecked;
  }

  void configure(std::string origin, const std::string &id = {}, const std::string &secret = {}) {
#ifdef USE_HOST
    if (origin.rfind("http://127.0.0.1:", 0) != 0 || !id.empty() || !secret.empty()) return;
#else
    if (origin.rfind("https://", 0) != 0 || id.empty() != secret.empty()) return;
#endif
    auto authority = origin.substr(origin.find("://") + 3);
    if (authority.empty() || authority.find_first_of("/?#@\r\n ") != std::string::npos) return;
    connection_ = std::make_shared<Connection>(Connection{origin, id, secret});
    ++generation_;
  }

  void request(std::string method, std::string path, std::string body, Done done, bool background = false) {
    if (method == "GET") {
      auto combine = [&](Task &t) {
        if (t.generation != generation_ || t.job.method != "GET" || t.job.path != path) return false;
        t.callbacks.push_back(std::move(done));
        if (!background) t.background = false;
        return true;
      };
      if (active_ && combine(*active_)) return;
      for (auto &task : queue_) if (combine(task)) return;
    } else ++generation_;  // A read requested after a write must observe that write.
    queue_.push_back({{connection_, std::move(method), std::move(path), std::move(body)},
                      {std::move(done)}, generation_, background});
  }

  void separate_reads() { ++generation_; }
  void promote(const std::string &prefix) {
    for (auto &task : queue_) if (task.job.path.rfind(prefix, 0) == 0) task.background = false;
  }

  void check_connection(std::function<void(bool)> done = {}) {
    request("GET", "/device/options/jars", {}, [this, done, connection = connection_](const Reply &r) {
      if (connection != connection_) { if (done) done(false); return; }
      bool ok = r.code == 200 && r.data["items"].is<JsonArrayConst>();
      status = ok ? ServiceStatus::Available : ServiceStatus::Unavailable;
      if (ok) ESP_LOGI("manager", "Connected to Glaze Manager");
      else ESP_LOGW("manager", "Inventory check failed (response code %d)", r.code);
      if (done) done(ok);
    });
  }

  void poll() {
    if (active_) {
      auto reply = worker_.take();
      if (!reply) return;
      finish(*reply);
    }
    if (queue_.empty()) return;
    auto next = std::find_if(queue_.begin(), queue_.end(), [](const Task &t) { return !t.background; });
    if (next == queue_.end()) next = queue_.begin();
    active_ = std::move(*next);
    queue_.erase(next);
    if (!worker_.start(active_->job)) finish({});
  }

 private:
  struct Task {
    Request job;
    std::vector<Done> callbacks;
    uint32_t generation;
    bool background;
  };
  void finish(const Reply &r) {
    auto task = std::move(*active_);
    active_.reset();
    if (task.job.connection == connection_) {
      status = (r.code == 200 || r.code == 201 || r.code == 404 || r.code == 409 || r.code == 422)
                  ? ServiceStatus::Available : ServiceStatus::Unavailable;
    }
    for (auto &done : task.callbacks) done(r);
  }
  std::shared_ptr<const Connection> connection_;
  uint32_t generation_ = 0;
  NetworkWorker worker_;
  std::deque<Task> queue_;
  std::optional<Task> active_;
};

inline std::string json_body(const JsonDocument &data) {
  std::string body;
  serializeJson(data, body);
  return body;
}

}  // namespace glaze
