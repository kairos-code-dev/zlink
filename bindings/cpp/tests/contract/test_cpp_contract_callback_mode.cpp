/* SPDX-License-Identifier: MPL-2.0 */

#include "../../samples/sample_common.hpp"

#include <chrono>
#include <cstring>
#include <optional>
#include <thread>

namespace {

bool wait_for_spot_ready (zlink::service::spot_node_t &node_,
                          bool require_subject_,
                          uint32_t min_active_peer_count_,
                          int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink::spot_node_status_t status;
        try {
            status = node_.status_snapshot ();
        } catch (...) {
            std::this_thread::yield ();
            continue;
        }

        const bool pub_ready =
          !status.local_endpoint ().empty ()
          && (min_active_peer_count_ == 0
              || status.active_peer_count () >= min_active_peer_count_);
        const bool sub_ready =
          require_subject_
            ? status.subject_count () > 0
                && (status.active_peer_count () >= min_active_peer_count_
                    || status.connected_peer_count () >= min_active_peer_count_)
            : status.active_peer_count () >= min_active_peer_count_;
        if (require_subject_ ? sub_ready : pub_ready)
            return true;
        std::this_thread::yield ();
    }

    return false;
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    zlink::service::discovery_t discovery (
      ctx, zlink::auto_connect_type::spot_mesh, detail::k_spot_service);
    zlink::service::spot_node_t node (ctx);
    assert (registry.valid ());
    assert (discovery.valid ());
    assert (node.valid ());

    zlink::service::spot_t spot = node.create_spot ();
    assert (spot.valid ());

    const std::string registry_pub = detail::unique_tcp ("spot-callback-reg-pub");
    const std::string registry_router =
      detail::unique_tcp ("spot-callback-reg-router");
    const std::string endpoint = detail::unique_tcp ("spot-callback-node");
    registry.bind (registry_pub, registry_router);
    discovery.connect_registry (registry_router);
    node.attach_discovery (discovery);
    node.bind (endpoint);
    spot.set_subscription ("topic:alpha");
    assert (wait_for_spot_ready (node, false, 0u, 10000));

    std::optional<zlink::topic_message_t> inbound;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t outbound =
          detail::make_message ("spot-callback");
        spot.publish (detail::k_spot_service, "topic:alpha")
          .message (outbound)
          .submit ();
        inbound = spot.subscribe (ZLINK_DONTWAIT);
        if (inbound.has_value ())
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (inbound.has_value ());
    assert (inbound->service_name ());
    assert (*inbound->service_name () == detail::k_spot_service);
    assert (inbound->topic () == "topic:alpha");
    assert (inbound->parts ().size () == 1);
    const std::string received = inbound->parts ()[0].to_string ();
    assert (received == "spot-callback");
    spot.close ();
    node.close ();
    return 0;
}
