/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace zlink::framework::e2e::registry_messaging::consumer
{

inline profile_reply_t request_profile_with_retry (zlink::framework::channel_client_t &channels,
                                                   const profile_request_t &request,
                                                   std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
    std::string last_error;
    while (std::chrono::steady_clock::now () < deadline) {
        auto call = channels.request (api_channel, request)
                      .timeout (timeout)
                      .async<profile_reply_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return reply.value ();
        }
        last_error = reply.error () ? reply.error ()->what () : "unknown profile request error";
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for profile endpoint: " + last_error);
}

inline payload_reply_t request_payload_with_retry (zlink::framework::channel_client_t &channels,
                                                   const payload_request_t &request)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
    std::string last_error;
    while (std::chrono::steady_clock::now () < deadline) {
        auto call = channels.request (api_channel, request)
                      .timeout (std::chrono::seconds (10))
                      .async<payload_reply_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return reply.value ();
        }
        last_error = reply.error () ? reply.error ()->what () : "unknown payload request error";
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for payload endpoint: " + last_error);
}

class batch_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = std::vector<profile_request_t>;
    using reply_type = std::vector<profile_reply_t>;

    explicit batch_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    std::vector<profile_reply_t> handle (const std::vector<profile_request_t> &requests)
    {
        std::vector<profile_reply_t> replies;
        replies.reserve (requests.size ());
        for (const auto &request : requests) {
            replies.push_back (
              request_profile_with_retry (_channels, request, std::chrono::seconds (5)));
        }
        return replies;
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_request_t;
    using reply_type = profile_reply_t;

    explicit profile_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    profile_reply_t handle (const profile_request_t &request)
    {
        return request_profile_with_retry (_channels, request, std::chrono::seconds (5));
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class slow_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_request_t;
    using reply_type = request_failure_result_t;

    explicit slow_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    request_failure_result_t handle (const profile_request_t &request)
    {
        auto call = _channels.request (api_channel, request)
                      .timeout (std::chrono::milliseconds (100))
                      .async<profile_reply_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return {.failed = false, .error_type = ""};
        }
        return {.failed = true,
                .error_type = reply.error () ? reply.error ()->what () : "request failed"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class missing_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_request_t;
    using reply_type = request_failure_result_t;

    explicit missing_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    request_failure_result_t handle (const profile_request_t &request)
    {
        auto call = _channels.request (api_channel, request)
                      .packet_name ("MissingProfileRequest")
                      .timeout (std::chrono::seconds (5))
                      .async<profile_reply_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return {.failed = false, .error_type = ""};
        }
        return {.failed = true,
                .error_type = reply.error () ? reply.error ()->what () : "request failed"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class missing_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_command_t;
    using reply_type = operation_status_t;

    explicit missing_command_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    operation_status_t handle (const profile_command_t &command)
    {
        auto send = _channels.send (api_channel, command)
                      .packet_name ("MissingProfileCommand")
                      .async ();
        (void) send.result ();
        return {.status = "sent"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class payload_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = payload_request_t;
    using reply_type = payload_reply_t;

    explicit payload_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    payload_reply_t handle (const payload_request_t &request)
    {
        return request_payload_with_retry (_channels, request);
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class backpressure_reset_handler_t
{
  public:
    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = R"({"status":"ready"})";
        return response;
    }
};

class backpressure_send_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_command_t;
    using reply_type = backpressure_send_result_t;

    explicit backpressure_send_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    backpressure_send_result_t handle (const profile_command_t &command)
    {
        auto send = _channels.send (api_channel, command)
                      .timeout (std::chrono::milliseconds (500))
                      .async ();
        const auto &result = send.result ();
        if (result) {
            return {.outcome = "Accepted"};
        }
        return {.outcome = "BoundedFailure"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

} // namespace zlink::framework::e2e::registry_messaging::consumer
