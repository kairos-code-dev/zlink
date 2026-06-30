/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/publisher_options.hpp"

#include <nlohmann/json.hpp>

namespace zlink::framework::e2e::pubsub::server::publisher
{

inline zlink::framework::http_response_t publish_from_query (
  zlink::framework::publisher_t &publisher,
  const zlink::framework::http_request_t &request,
  const char *packet_name = nullptr)
{
    const auto topic = request.query_values.find ("topic");
    const auto value = request.query_values.find ("value");
    if (topic == request.query_values.end () || value == request.query_values.end ()) {
        zlink::framework::http_response_t response;
        response.status = 400;
        response.body = R"({"error":"topic and value are required"})";
        return response;
    }

    auto call = publisher.publish (event_channel, topic->second, event_msg_t{value->second});
    if (packet_name != nullptr) {
        call.packet_name (packet_name);
    }
    auto result = call.async ().result ();
    if (!result.has_value ()) {
        zlink::framework::http_response_t response;
        response.status = 500;
        response.body = nlohmann::json{
          {"error", result.error () ? result.error ()->what () : "unknown publish error"}}
                          .dump ();
        return response;
    }

    zlink::framework::http_response_t response;
    response.body = nlohmann::json{
      {"status", "published"}, {"topic", topic->second}, {"value", value->second}}
                      .dump ();
    return response;
}

class publish_event_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::publisher_t>;

    explicit publish_event_handler_t (zlink::framework::publisher_t &publisher) :
        _publisher (publisher)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &request)
    {
        return publish_from_query (_publisher, request);
    }

  private:
    zlink::framework::publisher_t &_publisher;
};

class publish_missing_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::publisher_t>;

    explicit publish_missing_handler_t (zlink::framework::publisher_t &publisher) :
        _publisher (publisher)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &request)
    {
        return publish_from_query (_publisher, request, "MissingEventMsg");
    }

  private:
    zlink::framework::publisher_t &_publisher;
};

} // namespace zlink::framework::e2e::pubsub::server::publisher
