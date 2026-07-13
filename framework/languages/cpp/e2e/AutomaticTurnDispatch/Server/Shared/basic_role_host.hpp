/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "codecs.hpp"
#include "env.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::framework::e2e::automatic_turn_dispatch::server
{

inline int run_basic_http_role (int argc, char **argv, std::string role_name)
{
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (log_dir + "/" + role_name + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + role_name + "-flow.log")
          .trace_label ("cpp-atd-" + role_name);
        configure_codecs (options.codecs ());
        if (!http_endpoint.empty ()) {
            options.http ().listen (http_endpoint).map_health ("/health");
        }
    });
    return app.run (argc, argv);
}

} // namespace zlink::framework::e2e::automatic_turn_dispatch::server
