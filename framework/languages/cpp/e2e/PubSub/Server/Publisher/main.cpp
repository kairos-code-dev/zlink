/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/publisher_options.hpp"
#include "Endpoints/publisher_endpoints.hpp"

namespace ps_publisher = zlink::framework::e2e::pubsub::server::publisher;
namespace ps_server = zlink::framework::e2e::pubsub::server;

int main (int argc, char **argv)
{
    ps_publisher::publisher_options_t pubsub;
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (pubsub.log_dir + "/publisher.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        ps_server::configure_flow (options, pubsub.log_dir, "publisher");
        ps_server::configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (pubsub.registry_router);
        options.add_fanout_channel (zlink::framework::e2e::pubsub::event_channel)
          .enable_publisher (pubsub.publisher_endpoint);
        options.http ()
          .listen (pubsub.http_endpoint)
          .map_health ("/health")
          .map_post<ps_publisher::publish_event_handler_t> ("/publish/event")
          .map_post<ps_publisher::publish_missing_handler_t> ("/publish/missing");
    });
    return app.run (argc, argv);
}
