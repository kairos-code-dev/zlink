/* SPDX-License-Identifier: MPL-2.0 */

#include "Handlers/continue_order_workflow_handler.hpp"
#include "Handlers/query_and_self_check_handlers.hpp"
#include "Handlers/start_order_handler.hpp"
#include "../sample_log_dir.hpp"

#include <zlink/framework.hpp>

using namespace zlink;

#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

namespace
{

std::string workflow_endpoint ()
{
    if (const char *value = std::getenv ("SHOPPINGMALL_WORKFLOW_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32093";
}

std::string registry_pub_endpoint ()
{
    if (const char *value = std::getenv ("SHOPPINGMALL_REGISTRY_PUB_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32085";
}

std::string registry_router_endpoint ()
{
    if (const char *value = std::getenv ("SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32086";
}

} // namespace

int main (int argc, char **argv)
{
    using namespace zlink::samples::shoppingmall;
    using namespace framework;

    auto registry_app = app_t::create ();
    registry_app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path ("registry"))
          .trace_label ("shoppingmall-registry");
        options.enable_registry (registry_pub_endpoint (), registry_router_endpoint ());
    });
    std::thread registry_thread ([&] { (void) registry_app.run (argc, argv); });

    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path ("server"))
          .trace_label ("shoppingmall-server");
        options.services ().add_singleton<shopping_mall_server_role_t> (
          std::make_unique<shopping_mall_server_role_t> ());
        options.codecs ().add_json ();
        options.use_discovery ().add_registry_endpoint (registry_router_endpoint ());
        options.add_client_server_channel ("shoppingmall.workflow")
          .enable_server (workflow_endpoint ())
          .use_handler_group ("workflow");
        options.handlers ()
          .group ("workflow")
          .add<start_order_handler_t> ()
          .add<continue_order_workflow_handler_t> ()
          .add<get_order_state_handler_t> ()
          .add<delete_order_projection_handler_t> ()
          .add<rebuild_order_projection_handler_t> ()
          .add<seed_pending_idempotency_handler_t> ()
          .add<server_assertion_handler_t> ();
    });
    const auto exit_code = app.run (argc, argv);
    registry_app.stop ();
    if (registry_thread.joinable ()) {
        registry_thread.join ();
    }
    return exit_code;
}
