/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/resilience_lifecycle_contracts.hpp"
#include "../Configuration/consumer_options.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace zlink::framework::e2e::resilience_lifecycle::consumer
{

using resilience_lifecycle::api_channel;
using resilience_lifecycle::operation_status_t;
using resilience_lifecycle::profile_msg_t;
using resilience_lifecycle::profile_req_t;
using resilience_lifecycle::profile_res_t;
using resilience_lifecycle::request_failure_res_t;

inline profile_res_t request_profile_with_retry (zlink::framework::channel_client_t &channels,
                                                 const profile_req_t &request,
                                                 std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
    std::string last_error;
    while (std::chrono::steady_clock::now () < deadline) {
        auto call = channels.request (api_channel, request)
                      .timeout (timeout)
                      .async<profile_res_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return reply.value ();
        }
        last_error = reply.error () ? reply.error ()->what () : "unknown profile request error";
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for profile endpoint: " + last_error);
}

class profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit profile_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    profile_res_t handle (const profile_req_t &request)
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
    using request_type = profile_req_t;
    using reply_type = request_failure_res_t;

    explicit slow_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    request_failure_res_t handle (const profile_req_t &request)
    {
        auto call = _channels.request (api_channel, request)
                      .timeout (std::chrono::milliseconds (100))
                      .async<profile_res_t> ();
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
    using request_type = profile_req_t;
    using reply_type = request_failure_res_t;

    explicit missing_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    request_failure_res_t handle (const profile_req_t &request)
    {
        auto call = _channels.request (api_channel, request)
                      .packet_name ("MissingProfileReq")
                      .timeout (std::chrono::seconds (5))
                      .async<profile_res_t> ();
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

class profile_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_msg_t;
    using reply_type = operation_status_t;

    explicit profile_command_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    operation_status_t handle (const profile_msg_t &command)
    {
        _channels.send (api_channel, command).submit ();
        return {.status = "sent"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class transient_profile_request_service_t final : public zlink::framework::hosted_service_t
{
  public:
    transient_profile_request_service_t (zlink::framework::app_t &app, profile_req_t request) :
        _app (app), _request (std::move (request))
    {
    }

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto &channels = services.get_required<zlink::framework::channel_client_t> ();
            _reply = request_profile_with_retry (channels, _request, std::chrono::seconds (5));
        }
        catch (const std::exception &error) {
            _error = error.what ();
        }
        _app.stop ();
    }

    void stop () noexcept override {}

    profile_res_t take_reply () const
    {
        if (_error) {
            throw std::runtime_error (*_error);
        }
        if (!_reply) {
            throw std::runtime_error ("transient profile request produced no reply");
        }
        return *_reply;
    }

  private:
    zlink::framework::app_t &_app;
    profile_req_t _request;
    std::optional<profile_res_t> _reply;
    std::optional<std::string> _error;
};

inline profile_res_t request_profile_with_new_client_host (const consumer_options_t &options,
                                                           const profile_req_t &request)
{
    auto app = zlink::framework::app_t::create ();
    auto service = std::make_unique<transient_profile_request_service_t> (app, request);
    auto *service_result = service.get ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.use_discovery ().add_registry_endpoint (options.registry_router);
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/storm-" + request.value + "-flow.log")
          .trace_label ("storm-" + request.value);
        framework.add_client_server_channel (api_channel).enable_client ();
    });
    app.add_hosted_service (std::move (service));
    const auto exit_code = app.run (0, nullptr);
    if (exit_code != 0) {
        throw std::runtime_error ("transient profile request host failed");
    }
    return service_result->take_reply ();
}

class new_client_profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<consumer_options_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit new_client_profile_request_handler_t (consumer_options_t &options) :
        _options (options)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        return request_profile_with_new_client_host (_options, request);
    }

  private:
    consumer_options_t &_options;
};

} // namespace zlink::framework::e2e::resilience_lifecycle::consumer
