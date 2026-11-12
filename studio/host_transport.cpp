#include "esphome/core/defines.h"
#ifdef USE_HOST
#define USE_HTTP_REQUEST_HOST_H
#define CPPHTTPLIB_NO_EXCEPTIONS
#include "esphome/components/http_request/httplib.h"
#include "transport.h"

namespace glaze {
class HostConnection : public httplib::Client {
 public:
  using httplib::Client::Client;
};

Reply perform_host(std::shared_ptr<HostConnection> &client, const Request &job, size_t limit) {
  Reply reply;
  if (!client) {
    client = std::make_shared<HostConnection>(job.connection->origin);
    client->set_connection_timeout(2, 0);
    client->set_read_timeout(2, 0);
    client->set_keep_alive(true);
  }
  httplib::Request request;
  request.method = job.method;
  request.path = "/api/v1" + job.path;
  request.body = job.body;
  request.headers.emplace("Content-Type", "application/json");
  auto response = client->send(request);
  if (!response) { client.reset(); return reply; }
  reply.code = response->status;
  if ((reply.code == 200 || reply.code == 201) && job.method != "PUT" &&
    (response->body.size() > limit || deserializeJson(reply.data, response->body))) reply.code = 0;
  return reply;
}

}  // namespace glaze
#endif
