#pragma once

#include <ArduinoJson.h>
#include "esphome/core/version.h"
#include <memory>
#include <optional>
#include <string>
#include "esphome/core/log.h"

#ifndef USE_HOST
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#endif

namespace glaze {

struct Reply {
  int code = 0;
  JsonDocument data{};
};

struct Connection {
  std::string origin, access_id, access_secret;
};

struct Request {
  std::shared_ptr<const Connection> connection;
  std::string method, path, body;
};

#ifdef USE_HOST
class HostConnection;
Reply perform_host(std::shared_ptr<HostConnection> &client, const Request &job, size_t limit);
#endif

// Only the network worker touches the connection, including its TLS handle.
class Transport {
 public:
  Reply perform(const Request &job) {
    Reply reply;
    if (!job.connection) return reply;
    if (connection_ != job.connection) {
      close();
      connection_ = job.connection;
    }
    const size_t limit = job.path.rfind("/device/jars/", 0) == 0 ? 8192 : 4096;
#ifdef USE_HOST
    return perform_host(client_, job, limit);
#else
    std::string bytes;
    std::string url = connection_->origin + "/api/v1" + job.path;
    Body body{bytes, limit};
    if (!client_) {
      esp_http_client_config_t cfg{};
      cfg.url = url.c_str();
      cfg.user_agent = "ESPHome/" ESPHOME_VERSION " (https://esphome.io)";
      cfg.timeout_ms = 2000;
      cfg.crt_bundle_attach = esp_crt_bundle_attach;
      cfg.disable_auto_redirect = true;
      cfg.event_handler = receive;
      client_ = esp_http_client_init(&cfg);
      if (!client_) return reply;
      if (!connection_->access_id.empty()) {
        esp_http_client_set_header(client_, "CF-Access-Client-Id", connection_->access_id.c_str());
        esp_http_client_set_header(client_, "CF-Access-Client-Secret", connection_->access_secret.c_str());
      }
    }
    const auto method = job.method == "GET" ? HTTP_METHOD_GET :
                        job.method == "PUT" ? HTTP_METHOD_PUT : HTTP_METHOD_POST;
    if (esp_http_client_set_url(client_, url.c_str()) != ESP_OK ||
        esp_http_client_set_method(client_, method) != ESP_OK ||
        // ESP-IDF clears Content-Type when resetting the previous request body.
        esp_http_client_set_header(client_, "Content-Type", "application/json") != ESP_OK ||
        esp_http_client_set_post_field(client_, job.body.empty() ? nullptr : job.body.data(), job.body.size()) != ESP_OK ||
        esp_http_client_set_user_data(client_, &body) != ESP_OK) {
      close();
      return reply;
    }
    const auto result = esp_http_client_perform(client_);
    esp_http_client_set_user_data(client_, nullptr);
    if (result != ESP_OK) { close(); return reply; }
    reply.code = esp_http_client_get_status_code(client_);
    if (body.overflow && job.method != "PUT" && (reply.code == 200 || reply.code == 201)) return {};
    if ((reply.code == 200 || reply.code == 201) && job.method != "PUT" &&
        (bytes.size() > limit || deserializeJson(reply.data, bytes))) reply.code = 0;
    return reply;
#endif
  }

  ~Transport() { close(); }

 private:
  std::shared_ptr<const Connection> connection_;
#ifdef USE_HOST
  std::shared_ptr<HostConnection> client_;
  void close() { client_.reset(); }
#else
  esp_http_client_handle_t client_ = nullptr;
  struct Body { std::string &bytes; size_t limit; bool overflow = false; };
  static esp_err_t receive(esp_http_client_event_t *event) {
    auto *body = static_cast<Body *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && body && !body->overflow) {
      if (body->bytes.size() + event->data_len > body->limit) body->overflow = true;
      else body->bytes.append(static_cast<const char *>(event->data), event->data_len);
    }
    return ESP_OK;
  }
  void close() {
    if (client_) esp_http_client_cleanup(client_);
    client_ = nullptr;
  }
#endif
};
}  // namespace glaze
