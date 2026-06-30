/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/consumer_options.hpp"
#include "Endpoints/consumer_endpoints.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace rl_consumer = zlink::framework::e2e::resilience_lifecycle::consumer;

int main (int argc, char **argv)
{
    const auto options = rl_consumer::read_consumer_options ();
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/consumer.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/consumer-flow.log")
          .trace_label ("consumer");
        framework.services ().add_singleton<rl_consumer::consumer_options_t> (
          std::make_unique<rl_consumer::consumer_options_t> (options));
        auto channel = framework.add_client_server_channel (rl_consumer::api_channel);
        framework.use_discovery ().add_registry_endpoint (options.registry_router);
        channel.enable_client ();
        framework.http ()
          .listen (options.http_endpoint)
          .configure_server ([] (zlink::framework::http_server_options_builder_t &server) {
              server.set_max_request_body_size (2 * 1024 * 1024);
          })
          .map_health ("/health")
          .map_post<rl_consumer::profile_request_handler_t> ("/profile/request")
          .map_post<rl_consumer::slow_request_handler_t> ("/profile/request/timeout/100")
          .map_post<rl_consumer::missing_request_handler_t> ("/profile/request/missing")
          .map_post<rl_consumer::profile_command_handler_t> ("/profile/command")
          .map_post<rl_consumer::new_client_profile_request_handler_t> (
            "/profile/request/new-client");
    });
    return app.run (argc, argv);
}
