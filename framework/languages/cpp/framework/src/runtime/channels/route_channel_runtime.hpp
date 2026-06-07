/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/channels/channel_pending_requests.hpp"
#include "runtime/channels/route_connection_set.hpp"
#include "runtime/messaging/client_call_codec.hpp"

#include <zlink/Contracts/Core/routing_id.hpp>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

namespace backend
{
class native_route_backend_t;
} // namespace backend

struct route_outbound_packet_t
{
    zlink::routing_id_t target_node_rid;
    std::optional<zlink::routing_id_t> target_spot_rid;
    runtime::messaging::message_parts_t parts;
    std::optional<std::uint64_t> request_seq;
};

class route_channel_runtime_t
{
  public:
    using send_backend_t = std::function<result_t<void> (
      const zlink::routing_id_t &, const runtime::messaging::message_parts_t &)>;
    using request_backend_t = std::function<result_t<runtime::messaging::message_parts_t> (
      const zlink::routing_id_t &,
      const runtime::messaging::message_parts_t &,
      std::chrono::milliseconds)>;

    explicit route_channel_runtime_t (std::string router_channel_id);

    const std::string &router_channel_id () const noexcept;
    void routing_id (zlink::routing_id_t routing_id);
    const std::optional<zlink::routing_id_t> &routing_id () const noexcept;
    void spot_route_egress_target (std::string target_spot_node_channel_name);
    const std::optional<std::string> &spot_route_egress_target () const noexcept;
    void start () noexcept;
    void stop () noexcept;
    bool running () const noexcept;

    bool connect (std::string endpoint);
    bool disconnect (const std::string &endpoint);
    std::vector<std::string> list_connections () const;

    template <typename TMessage>
    result_t<void> submit_send (const zlink::routing_id_t &target_node_rid,
                                std::string packet_name,
                                const TMessage &message,
                                const serializer_registry_t &serializers)
    {
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (runtime::messaging::message_kind_t::command,
                                             _router_channel_id, std::move (packet_name));
        return submit_send_parts (target_node_rid,
                                  codec.encode_envelope_parts (header, message, serializers));
    }

    template <typename TRequest>
    result_t<std::uint64_t> submit_request (const zlink::routing_id_t &target_node_rid,
                                            std::string packet_name,
                                            const TRequest &request,
                                            const serializer_registry_t &serializers,
                                            std::chrono::milliseconds timeout)
    {
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (runtime::messaging::message_kind_t::request,
                                             _router_channel_id, std::move (packet_name), timeout);
        return submit_request_parts (target_node_rid,
                                     codec.encode_envelope_parts (header, request, serializers));
    }

    result_t<void> submit_send_parts (const zlink::routing_id_t &target_node_rid,
                                      runtime::messaging::message_parts_t parts);

    result_t<std::uint64_t> submit_request_parts (const zlink::routing_id_t &target_node_rid,
                                                  runtime::messaging::message_parts_t parts);

    result_t<runtime::messaging::message_parts_t>
    request_reply_parts (const zlink::routing_id_t &target_node_rid,
                         runtime::messaging::message_parts_t parts,
                         std::chrono::milliseconds timeout);

    result_t<void> submit_spot_send_parts (const zlink::routing_id_t &target_node_rid,
                                           const zlink::routing_id_t &target_spot_rid,
                                           runtime::messaging::message_parts_t parts);

    result_t<std::uint64_t> request_to_spot_parts (const zlink::routing_id_t &target_node_rid,
                                                   const zlink::routing_id_t &target_spot_rid,
                                                   runtime::messaging::message_parts_t parts);

    result_t<void> complete_request (std::uint64_t request_seq);
    void set_send_backend (send_backend_t backend);
    void set_request_backend (request_backend_t backend);
    void attach_native_backend (backend::native_route_backend_t &backend);

    const std::vector<route_outbound_packet_t> &outbound_packets () const noexcept;
    std::size_t pending_request_count () const noexcept;

  private:
    result_t<void> ensure_connected () const;

    std::string _router_channel_id;
    std::optional<zlink::routing_id_t> _routing_id;
    std::optional<std::string> _spot_route_egress_target;
    bool _running = false;
    route_connection_set_t _connections;
    channel_pending_requests_t _pending_requests;
    std::vector<route_outbound_packet_t> _outbound_packets;
    send_backend_t _send_backend;
    request_backend_t _request_backend;
};

} // namespace zlink::framework::detail
