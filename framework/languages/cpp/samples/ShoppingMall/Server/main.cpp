/* SPDX-License-Identifier: MPL-2.0 */

#include "Handlers/continue_order_workflow_handler.hpp"
#include "Handlers/query_and_self_check_handlers.hpp"
#include "Handlers/start_order_handler.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <memory>
#include <string>

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

} // namespace

int main (int argc, char **argv)
{
    using namespace zlink::samples::shoppingmall;

    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
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
        options.add_client_server_channel ("shoppingmall.workflow")
          .enable_server (workflow_endpoint ())
          .use_handler_group ("workflow");
    });
    return app.run (argc, argv);
}
