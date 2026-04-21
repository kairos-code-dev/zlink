/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::socket, detail::k_spot_service);
    zlink::service::registry_query_client_t query (ctx);
    zlink::pub_socket_t provider (ctx);
    assert (registry.valid ());
    assert (discovery.valid ());
    assert (query.valid ());
    assert (provider.valid ());

    const std::string registry_pub = detail::unique_tcp ("registry-pub");
    const std::string registry_router = detail::unique_tcp ("registry-router");
    const std::string service_endpoint = detail::unique_tcp ("registry-service");
    registry.bind (registry_pub, registry_router);
    discovery.connect_registry (registry_router);
    provider.attach_discovery (discovery);
    provider.bind (service_endpoint);
    query.connect (registry_router);

    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    bool found = false;
    while (std::chrono::steady_clock::now () < deadline) {
        try {
            const auto entries = query.snapshot ();
            for (const auto &entry : entries) {
                if (entry.service_name == detail::k_spot_service) {
                    found = true;
                    break;
                }
            }
        }
        catch (const zlink::config_error_t &) {
        }
        if (found)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (25));
    }

    assert (found);
    std::printf (
      "[registry-query] service: \"%s\" -> snapshot: found\n",
      detail::k_spot_service);
    return 0;
}
