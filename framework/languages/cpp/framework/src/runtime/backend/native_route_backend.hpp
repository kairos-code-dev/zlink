/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/messaging/envelope_codec.hpp"

#include <zlink/Contracts/Core/routing_id.hpp>

#include <chrono>

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

  result_t<void> submit_send (
    const zlink::routing_id_t &target_node_rid,
    const runtime::messaging::message_parts_t &parts);

  result_t<runtime::messaging::message_parts_t> submit_request (
    const zlink::routing_id_t &target_node_rid,
    const runtime::messaging::message_parts_t &parts,
    std::chrono::milliseconds timeout);

private:
  zlink::router_socket_t *_router;
};

} // namespace zlink::framework::detail::backend
