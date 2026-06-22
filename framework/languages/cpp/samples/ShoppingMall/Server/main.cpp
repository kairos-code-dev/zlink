/* SPDX-License-Identifier: MPL-2.0 */

#include "Handlers/continue_order_workflow_handler.hpp"
#include "Handlers/query_and_self_check_handlers.hpp"
#include "Handlers/start_order_handler.hpp"
#include "../sample_log_dir.hpp"

#include <zlink/framework.hpp>

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

    auto registry_app = zlink::framework::app_t::create ();
    registry_app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path ("registry"))
          .trace_node_id ("shoppingmall-registry");
        options.enable_registry (registry_pub_endpoint (), registry_router_endpoint ());
    });
    std::thread registry_thread ([&] { (void) registry_app.run (argc, argv); });

    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path ("server"))
          .trace_node_id ("shoppingmall-server");
        options.services ().add_singleton<shopping_mall_server_role_t> (
          std::make_unique<shopping_mall_server_role_t> ());
        options.handlers ()
          .add<start_order_handler_t> ("workflow")
          .add<continue_order_workflow_handler_t> ("workflow")
          .add<get_order_state_handler_t> ("workflow")
          .add<delete_order_projection_handler_t> ("workflow")
          .add<rebuild_order_projection_handler_t> ("workflow")
          .add<seed_pending_idempotency_handler_t> ("workflow")
          .add<server_assertion_handler_t> ("workflow");
        options.codecs ()
          .add_json ()
          .add_json<start_order_req_t> ()
          .add_json<start_order_res_t> ()
          .add_json<get_order_state_req_t> ()
          .add_json<delete_order_projection_req_t> ()
          .add_json<get_order_state_res_t> ()
          .add_json<continue_order_workflow_req_t> ()
          .add_json<continue_order_workflow_res_t> ()
          .add_json<rebuild_order_projection_req_t> ()
          .add_json<rebuild_order_projection_res_t> ()
          .add_json<seed_pending_idempotency_req_t> ()
          .add_json<server_assertion_req_t> ()
          .add_json<server_assertion_res_t> ()
          .add_json<order_state_t> ();
        options.use_discovery ().add_registry_endpoint (registry_router_endpoint ());
        options.add_client_server_channel ("shoppingmall.workflow")
          .enable_server (workflow_endpoint ())
          .use_handler_group ("workflow");
    });
    const auto exit_code = app.run (argc, argv);
    registry_app.stop ();
    if (registry_thread.joinable ()) {
        registry_thread.join ();
    }
    return exit_code;
}
