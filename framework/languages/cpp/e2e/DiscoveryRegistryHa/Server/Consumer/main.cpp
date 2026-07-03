/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/consumer_options.hpp"
#include "Endpoints/consumer_endpoints.hpp"

#include "../../Shared/store_failure_contracts.hpp"
#include "../Shared/location_store.hpp"

#include <zlink/framework.hpp>

namespace sf = zlink::framework::e2e::store_failure;
namespace sf_consumer = zlink::framework::e2e::store_failure::consumer;

int main (int argc, char **argv)
{
    const auto options = sf_consumer::read_consumer_options ();
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/" + options.rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/" + options.rid + "-flow.log")
          .trace_label ("cpp-store-failure-" + options.rid);
        sf::server::add_redis_location_store (framework, options.redis_endpoint,
                                              options.redis_key_prefix);
        framework.add_client_server_channel (sf::api_channel).enable_client ();
        framework.http ()
          .listen (options.http_endpoint)
          .map_health ("/health")
          .map_get<sf_consumer::query_status_handler_t> ("/query/status")
          .map_get<sf_consumer::query_peers_handler_t> ("/query/peers")
          .map_post<sf_consumer::profile_request_handler_t> ("/profile/request")
          .map_post<sf_consumer::profile_request_timeout_handler_t> (
            "/profile/request/timeout/{milliseconds}");
    });
    return app.run (argc, argv);
}
