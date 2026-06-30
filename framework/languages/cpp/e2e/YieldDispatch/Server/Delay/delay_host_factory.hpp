/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Shared/codecs.hpp"
#include "../Shared/env.hpp"
#include "Handlers/delay_handler.hpp"
#include "Support/delay_support.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace zlink::framework::e2e::yield_dispatch::server::delay {

namespace yd = zlink::framework::e2e::yield_dispatch;

inline zlink::framework::app_t create_delay_host ()
{
    const auto log_dir = server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = server::env_or ("ZLINK_CPP_E2E_NODE_RID", "delay-a");
    const auto http_endpoint = server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto delay_endpoint = server::env_or ("ZLINK_CPP_E2E_DELAY_ENDPOINT");

    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (log_dir + "/" + node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([=] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-yd-" + node_rid);
        auto state = std::make_unique<delay_state_t> (node_rid);
        options.services ().add_singleton<delay_state_t> (std::move (state));
        server::configure_codecs (options.codecs ());
        options.add_client_server_channel (yd::delay_channel)
          .enable_server (delay_endpoint)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .use_handler_group (yd::handler_group);
        options.handlers ().group (yd::handler_group).add<delay_handler_t> ();
        options.http ().listen (http_endpoint).map_health ("/health");
    });
    return app;
}

} // namespace zlink::framework::e2e::yield_dispatch::server::delay
