/* SPDX-License-Identifier: MPL-2.0 */

#include "Handlers/authorize_payment_handler.hpp"
#include "Handlers/confirm_order_handler.hpp"
#include "Handlers/reserve_inventory_handler.hpp"
#include "Handlers/start_checkout_handler.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{

std::string checkout_endpoint ()
{
    if (const char *value = std::getenv ("SHOPPINGMALLCHECKOUT_CHECKOUT_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32094";
}

} // namespace

int main (int argc, char **argv)
{
    using namespace zlink::samples::shoppingmallcheckout;

    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.services ().add_singleton<shopping_mall_checkout_server_role_t> (
          std::make_unique<shopping_mall_checkout_server_role_t> ());
        options.handlers ()
          .add<start_checkout_handler_t> ("checkout")
          .add<authorize_payment_handler_t> ("checkout")
          .add<reserve_inventory_handler_t> ("checkout")
          .add<confirm_order_handler_t> ("checkout");
        options.codecs ()
          .add_json ()
          .add_json<start_checkout_req_t> ()
          .add_json<authorize_payment_req_t> ()
          .add_json<reserve_inventory_req_t> ()
          .add_json<confirm_order_req_t> ()
          .add_json<checkout_state_t> ();
        options.add_client_server_channel ("shoppingmallcheckout.checkout")
          .enable_server (checkout_endpoint ())
          .use_handler_group ("checkout");
    });
    return app.run (argc, argv);
}
