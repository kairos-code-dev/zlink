/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/codecs.hpp"
#include "../Shared/env.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace yd = zlink::framework::e2e::yield_dispatch;
namespace yd_server = zlink::framework::e2e::yield_dispatch::server;

struct delay_state_t
{
    explicit delay_state_t (std::string rid) : node_rid (std::move (rid)) {}
    std::string node_rid;
};

class delay_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<delay_state_t>;
    using request_type = yd::delay_req_t;
    using reply_type = yd::delay_reply_t;

    explicit delay_handler_t (delay_state_t &state) : _state (state) {}

    yd::delay_reply_t handle (const yd::delay_req_t &request)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (request.delay_ms));
        return {.request_id = request.request_id,
                .marker = request.marker,
                .node_rid = _state.node_rid};
    }

  private:
    delay_state_t &_state;
};

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    const auto log_dir = yd_server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = yd_server::env_or ("ZLINK_CPP_E2E_NODE_RID", "delay-a");
    const auto http_endpoint = yd_server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto delay_endpoint = yd_server::env_or ("ZLINK_CPP_E2E_DELAY_ENDPOINT");

    auto app = app_t::create ();
    app.logging ().use_file (log_dir + "/" + node_rid + ".log").set_min_level (log_level_t::debug);
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-yd-" + node_rid);
        auto state = std::make_unique<delay_state_t> (node_rid);
        options.services ().add_singleton<delay_state_t> (std::move (state));
        yd_server::configure_codecs (options.codecs ());
        options.add_client_server_channel (yd::delay_channel)
          .enable_server (delay_endpoint)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .use_handler_group (yd::handler_group);
        options.handlers ().group (yd::handler_group).add<delay_handler_t> ();
        options.http ().listen (http_endpoint).map_health ("/health");
    });
    return app.run (argc, argv);
}
