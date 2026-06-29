/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/messaging/envelope_codec.hpp"

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/Contracts/Service/spot_node.hpp>

#include <chrono>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zlink
{
class router_socket_t;
} // namespace zlink

namespace zlink::framework::detail::backend
{

class native_route_backend_t
{
  public:
    explicit native_route_backend_t (zlink::router_socket_t &router);
    native_route_backend_t (zlink::router_socket_t &router, std::atomic_bool &stop);
    native_route_backend_t (zlink::router_socket_t &router,
                            std::atomic_bool &stop,
                            std::vector<std::string> reconnect_endpoints);

    void attach_spot_route_bridge (std::unique_ptr<zlink::service::spot_route_bridge_t> bridge,
                                   std::string channel_name);

    result_t<void> submit_send (const zlink::routing_id_t &target_node_rid,
                                const std::optional<zlink::routing_id_t> &target_spot_rid,
                                const runtime::messaging::message_parts_t &parts);

    result_t<runtime::messaging::message_parts_t>
    submit_request (const zlink::routing_id_t &target_node_rid,
                    const std::optional<zlink::routing_id_t> &target_spot_rid,
                    const runtime::messaging::message_parts_t &parts,
                    std::chrono::milliseconds timeout);

    bool handle_router_received (const zlink::routing_id_t &source_node_rid,
                                 std::vector<zlink::message_t> &parts,
                                 std::optional<std::uint64_t> request_seq);

    int drain_spot_route_bridge ();
    void close () noexcept;

  private:
    bool stopping () const noexcept;
    void forget_peer (const zlink::routing_id_t &target_node_rid) noexcept;

    zlink::router_socket_t *_router;
    std::atomic_bool *_stop = nullptr;
    std::vector<std::string> _reconnect_endpoints;
    std::unique_ptr<zlink::service::spot_route_bridge_t> _spot_route_bridge;
    std::string _spot_route_channel_name;
};

} // namespace zlink::framework::detail::backend
