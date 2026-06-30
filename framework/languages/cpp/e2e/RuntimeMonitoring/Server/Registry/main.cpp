/* SPDX-License-Identifier: MPL-2.0 */

#include "Support/registry_options.hpp"

#include "../Shared/evidence_store.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <memory>
#include <string>

namespace rm = zlink::framework::e2e::runtime_monitoring;
namespace rm_registry = zlink::framework::e2e::runtime_monitoring::registry;
namespace rm_server = zlink::framework::e2e::runtime_monitoring::server;

namespace
{

std::string registry_kind_name (zlink::framework::registry_event_kind_t kind)
{
    switch (kind) {
        case zlink::framework::registry_event_kind_t::status_changed:
            return "StatusChanged";
        case zlink::framework::registry_event_kind_t::topology_changed:
            return "TopologyChanged";
        case zlink::framework::registry_event_kind_t::service_summary_changed:
            return "ServiceSummaryChanged";
    }
    return "Unknown";
}

} // namespace

int main (int argc, char **argv)
{
    using namespace std::chrono_literals;

    const auto options = rm_registry::read_registry_options ();
    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        auto evidence = std::make_unique<rm_server::evidence_store_t> (options.rid,
                                                                       options.evidence_file);
        auto *evidence_ptr = evidence.get ();
        framework.services ().add_singleton<rm_server::evidence_store_t> (std::move (evidence));
        framework.enable_registry (options.pub_endpoint, options.router_endpoint);
        framework.monitoring ()
          .add_registry_events ("registry", 100ms)
          .on<zlink::framework::registry_event_payload_t> (
            [evidence_ptr] (const zlink::framework::registry_event_payload_t &event) {
                evidence_ptr->add ("monitor-registry|source=" + event.source_name
                                   + "|kind=" + registry_kind_name (event.event)
                                   + "|topology=" + std::to_string (event.topology.size ())
                                   + "|summary="
                                   + std::to_string (event.service_summary.size ()));
            });
        if (!options.http_endpoint.empty ()) {
            framework.http ()
              .listen (options.http_endpoint)
              .map_health ("/health")
              .map_get<rm_server::evidence_handler_t> ("/evidence")
              .map_post<rm_server::evidence_wait_handler_t> ("/evidence/wait");
        }
    });
    return app.run (argc, argv);
}
