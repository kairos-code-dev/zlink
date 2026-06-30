/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Support/env.hpp"

#include <zlink/framework.hpp>

inline int run_registry_server (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto pub = env_or ("ZLINK_CPP_E2E_REGISTRY_PUB");
    const auto router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    app.logging ()
      .use_file (log_dir + "/registry.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/registry-flow.log")
          .trace_label ("cpp-sm-registry");
        options.enable_registry (pub, router);
    });
    return app.run (argc, argv);
}
