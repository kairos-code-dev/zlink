/* SPDX-License-Identifier: MPL-2.0 */

#include "../../samples/sample_common.hpp"

#include <chrono>
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

        const bool pub_ready = !status.local_endpoint.empty ();
        const bool sub_ready = status.active_peer_count >= min_active_peer_count_
                               && (!require_subject_ || status.subject_count > 0);
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
    zlink::service::spot_node_t pub_node (ctx);
    zlink::service::spot_node_t sub_node (ctx);
    assert (pub_node.valid ());
    assert (sub_node.valid ());

    const std::string service_name = detail::k_spot_service;
    zlink::service::registry_t registry (ctx);
    const std::string registry_pub = detail::unique_tcp ("spot-callback-reg-pub");
    const std::string registry_router =
      detail::unique_tcp ("spot-callback-reg-router");
    registry.bind (registry_pub, registry_router);
    zlink::service::discovery_t pub_discovery (
      ctx, zlink::service_type::spot, service_name);
    zlink::service::discovery_t sub_discovery (
      ctx, zlink::service_type::spot, service_name);
    assert (pub_discovery.valid ());
    assert (sub_discovery.valid ());
    pub_discovery.connect_registry (registry_router);
    sub_discovery.connect_registry (registry_router);
    pub_node.attach_discovery (pub_discovery);
    sub_node.attach_discovery (sub_discovery);

    const std::string pub_endpoint = detail::unique_tcp ("spot-callback-pub");
    const std::string sub_endpoint = detail::unique_tcp ("spot-callback-sub");
    pub_node.bind (pub_endpoint);
    sub_node.bind (sub_endpoint);

    zlink::service::spot_t pub_spot = pub_node.create_spot ();
    zlink::service::spot_t sub_spot = sub_node.create_spot ();
    assert (pub_spot.valid ());
    assert (sub_spot.valid ());

    sub_spot.set_subscription ("topic:alpha");
    assert (wait_for_spot_ready (pub_node, false, 1u, 10000));
    assert (wait_for_spot_ready (sub_node, true, 1u, 10000));

    std::optional<zlink::topic_message_t> inbound;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t outbound =
          detail::make_message ("spot-callback");
        pub_spot.publish (service_name, "topic:alpha", outbound);
        inbound = sub_spot.subscribe (zlink::recv_flags_t::dontwait);
        if (inbound.has_value ())
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (inbound.has_value ());
    assert (inbound->service_name ());
    assert (*inbound->service_name () == service_name);
    assert (inbound->topic () == "topic:alpha");
    assert (inbound->parts ().size () == 1);
    const std::string received = inbound->parts ()[0].to_string ();
    assert (received == "spot-callback");
    sub_spot.close ();
    pub_spot.close ();
    sub_node.close ();
    pub_node.close ();
    return 0;
}
