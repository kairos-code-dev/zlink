/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Support/server_host.hpp"

#include <iostream>

namespace rc_server = zlink::framework::e2e::registration_codec::server;

int main (int argc, char **argv)
{
    const auto api_endpoint = rc_server::env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
    const auto http_endpoint = rc_server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto log_dir = rc_server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");

    auto app = zlink::framework::app_t::create ();
    app.logging ().use_file (log_dir + "/codec-requester.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/codec-requester-flow.log")
          .trace_label ("cpp-rc-codec-requester");
        rc_server::add_binary_codecs (options.codecs ());
        rc_server::add_custom_codecs (options.codecs ());
        options.add_client_server_channel (
                 zlink::framework::e2e::registration_codec::api_channel)
          .enable_client (api_endpoint);
        options.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_post<rc_server::codec_mismatch_handler_t> ("/codec/mismatch");
    });

    try {
        return app.run (argc, argv);
    }
    catch (const std::exception &error) {
        std::cerr << "registration-codec codec requester failed: " << error.what () << "\n";
        return 1;
    }
}
