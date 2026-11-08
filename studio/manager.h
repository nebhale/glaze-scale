#pragma once

#include "esphome/components/http_request/http_request.h"

namespace glaze {

struct Reply {
  int code = 0;
  JsonDocument data;
};

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

  void network_changed() {
    // Verify prior successes on the new network. Keep a failed service retryable.
    if (status == ServiceStatus::Available) status = ServiceStatus::Unchecked;
  }

  void configure(esphome::http_request::HttpRequestComponent *http, std::string origin,
                 const std::string &access_id = {}, const std::string &access_secret = {}) {
    // The device sends credentials only to its flash-configured HTTPS origin.
    const bool tls = origin.compare(0, 8, "https://") == 0;
    if (!tls) {
#ifdef USE_HOST
      if (origin.compare(0, 17, "http://127.0.0.1:") != 0 || !access_id.empty() || !access_secret.empty()) return;
#else
      return;
#endif
    }
    auto authority = origin.substr(origin.find("://") + 3);
    if (authority.empty() || authority.find_first_of("/?#@\r\n ") != std::string::npos) return;
    http_ = http;
    base_ = origin + "/api/v1";
    headers_ = {{"Content-Type", "application/json"}};
    if (!access_id.empty() && !access_secret.empty()) {
      headers_.push_back({"CF-Access-Client-Id", access_id});
      headers_.push_back({"CF-Access-Client-Secret", access_secret});
    }
  }

  bool check_connection() {
    auto reply = call("GET", "/device/options/jars");
    const bool available = reply.code == 200 && reply.data["items"].is<JsonArray>();
    status = available ? ServiceStatus::Available : ServiceStatus::Unavailable;
    return available;
  }

  Reply call(const char *method, const std::string &path, const std::string &body = {}) {
    Reply reply;
    status = ServiceStatus::Unavailable;
    if (!http_) return reply;
    auto response = http_->start((base_ + path).c_str(), method, body, headers_);
    if (!response) return reply;
    reply.code = response->status_code;
    // Binding returns a full web jar. The subsequent lookup reads its device projection.
    if ((reply.code == 200 || reply.code == 201) && strcmp(method, "PUT") != 0) {
      size_t limit = path.find("/device/jars/") == 0 ? 8192 : 4096;
      if (response->content_length > limit) reply.code = 0;
      else {
        std::string bytes(response->content_length ? response->content_length : limit + 1, '\0');
        auto read = esphome::http_request::http_read_fully(response.get(),
            reinterpret_cast<uint8_t *>(bytes.data()), bytes.size(), 256, http_->get_timeout());
        auto size = response->get_bytes_read();
        if (read.status != esphome::http_request::HttpReadStatus::OK || !response->is_read_complete() ||
            size > limit || deserializeJson(reply.data, bytes.data(), size)) reply.code = 0;
      }
    }
    response->end();
    // A missing jar, edit conflict, or rejected input still came from the API.
    if (reply.code == 200 || reply.code == 201 || reply.code == 404 || reply.code == 409 || reply.code == 422)
      status = ServiceStatus::Available;
    return reply;
  }

 private:
  esphome::http_request::HttpRequestComponent *http_ = nullptr;
  std::string base_;
  std::vector<esphome::http_request::Header> headers_;
};

inline std::string json_body(const JsonDocument &data) {
  std::string body;
  serializeJson(data, body);
  return body;
}

}  // namespace glaze
