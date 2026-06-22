/* SPDX-License-Identifier: MPL-2.0 */

#include "Handlers/advance_delivery_handler.hpp"
#include "Handlers/assign_delivery_handler.hpp"
#include "Handlers/create_delivery_handler.hpp"
#include "Handlers/server_assertion_handler.hpp"
#include "Handlers/subscribe_delivery_handler.hpp"
#include "../sample_log_dir.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

namespace
{

std::string dispatch_endpoint ()
{
    if (const char *value = std::getenv ("DELIVERYDISPATCH_DISPATCH_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32091";
}

std::string registry_pub_endpoint ()
{
    if (const char *value = std::getenv ("DELIVERYDISPATCH_REGISTRY_PUB_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32081";
}

std::string registry_router_endpoint ()
{
    if (const char *value = std::getenv ("DELIVERYDISPATCH_REGISTRY_ROUTER_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32082";
}

} // namespace

int main (int argc, char **argv)
{
    using namespace zlink::samples::deliverydispatch;

    auto registry_app = zlink::framework::app_t::create ();
    registry_app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path ("registry"))
          .trace_node_id ("deliverydispatch-registry");
        options.enable_registry (registry_pub_endpoint (), registry_router_endpoint ());
    });
    std::thread registry_thread ([&] { (void) registry_app.run (argc, argv); });

    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path ("server"))
          .trace_node_id ("deliverydispatch-server");
        options.services ().add_singleton<delivery_dispatch_server_role_t> (
          std::make_unique<delivery_dispatch_server_role_t> ());
        options.handlers ()
          .add<create_delivery_handler_t> ("dispatch")
          .add<subscribe_delivery_handler_t> ("dispatch")
          .add<assign_delivery_handler_t> ("dispatch")
          .add<advance_delivery_handler_t> ("dispatch")
          .add<server_assertion_handler_t> ("dispatch");
        options.codecs ()
          .add_json ()
          .add_json<create_delivery_req_t> ()
          .add_json<delivery_created_t> ()
          .add_json<subscribe_delivery_req_t> ()
          .add_json<subscribe_delivery_accepted_t> ()
          .add_json<assign_delivery_req_t> ()
          .add_json<advance_delivery_req_t> ()
          .add_json<delivery_state_t> ()
          .add_json<server_assertion_req_t> ()
          .add_json<server_assertion_res_t> ();
        options.use_discovery ().add_registry_endpoint (registry_router_endpoint ());
        options.add_client_server_channel ("deliverydispatch.dispatch")
          .enable_server (dispatch_endpoint ())
          .use_handler_group ("dispatch");
    });
    const auto exit_code = app.run (argc, argv);
    registry_app.stop ();
    if (registry_thread.joinable ()) {
        registry_thread.join ();
    }
    return exit_code;
}
