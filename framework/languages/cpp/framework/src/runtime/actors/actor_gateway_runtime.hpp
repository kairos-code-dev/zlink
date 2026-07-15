/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/dispatch/execution.hpp>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

void enter_stream_relay_dispatch (const stream_header_t &header);
void exit_stream_relay_dispatch () noexcept;
std::optional<stream_header_t> current_stream_relay_dispatch ();

struct actor_bound_session_route_t
{
    zlink::routing_id_t node_rid;
    std::optional<zlink::routing_id_t> session_rid;
};

struct actor_record_t
{
    actor_ref_t ref;
    bool bound = false;
    bool disconnected = false;
    stream_codec_t bound_session_codec = stream_codec_t::message_pack;
    bool bound_session_stream_sink = false;
    std::optional<zlink::message_t> create_payload;
    std::optional<actor_bound_session_route_t> bound_session_route;
};

struct relayed_frame_t
{
    actor_ref_t actor;
    stream_header_t header;
    zlink::message_t payload;
};

class actor_gateway_state_t
{
  public:
    mutable std::recursive_mutex mutex;
    using join_spot_dispatcher_t = std::function<result_t<actor_join_reply_t> (
      const actor_ref_t &, spot_rid_t, const zlink::message_t &)>;
    using join_entry_spot_dispatcher_t = std::function<result_t<actor_join_reply_t> (
      const actor_ref_t &, node_rid_t, const zlink::message_t &)>;
    using relay_dispatcher_t = std::function<result_t<std::optional<zlink::message_t>> (
      const actor_ref_t &, actor_context_t, const stream_header_t &, const zlink::message_t &)>;
    using disconnect_dispatcher_t = std::function<result_t<void> (const actor_ref_t &)>;
    using bound_session_registrar_t = std::function<result_t<void> (const actor_ref_t &)>;
    using membership_query_t = std::function<std::optional<spot_rid_t> (const actor_ref_t &)>;

    std::map<std::string, actor_record_t> actors_by_id;
    std::map<std::string, std::function<task_t<void> (std::string, const zlink::message_t &)>>
      bound_session_sinks;
    std::vector<relayed_frame_t> relayed_frames;
    std::vector<relayed_frame_t> bound_session_pushes;
    join_spot_dispatcher_t join_spot_dispatcher;
    join_entry_spot_dispatcher_t join_entry_spot_dispatcher;
    relay_dispatcher_t relay_dispatcher;
    disconnect_dispatcher_t disconnect_dispatcher;
    bound_session_registrar_t bound_session_registrar;
    membership_query_t membership_query;
    serializer_registry_t *serializers = nullptr;
    dispatch_options_t dispatch;
};

class actor_gateway_runtime_t
{
  public:
    actor_gateway_runtime_t ();
    explicit actor_gateway_runtime_t (std::shared_ptr<actor_gateway_state_t> state);

    session_actor_manager_t manager () const;
    actor_gateway_t gateway () const;
    std::vector<relayed_frame_t> relayed_frames () const;
    std::vector<relayed_frame_t> bound_session_pushes () const;
    std::optional<actor_bound_session_route_t>
    bound_session_route (const actor_ref_t &actor_ref) const;
    bool actor_bound (std::string actor_id) const;
    bool actor_disconnected (std::string actor_id) const;
    actor_context_t actor_context (const actor_ref_t &actor_ref) const;
    result_t<void> update_actor_ref (const actor_ref_t &actor_ref);
    result_t<void> destroy_actor (const actor_ref_t &actor_ref);
    void bind_session_stream (std::string actor_id,
                              stream_t stream,
                              stream_codec_t codec = stream_codec_t::message_pack);
    void bind_session_route (actor_ref_t actor_ref,
                             route_client_t route_client,
                             std::string route_channel_name,
                             zlink::routing_id_t target_node_rid,
                             stream_codec_t codec = stream_codec_t::message_pack,
                             bool replace_existing = true);
    void
    bind_session_sink (actor_ref_t actor_ref,
                       std::function<task_t<void> (std::string, const zlink::message_t &)> sink,
                       stream_codec_t codec = stream_codec_t::message_pack,
                       bool replace_existing = true);
    void record_bound_session_route (const actor_ref_t &actor_ref,
                                     zlink::routing_id_t node_rid,
                                     std::optional<zlink::routing_id_t> session_rid = std::nullopt);
    void unbind_session_stream (std::string actor_id);
    result_t<void> dispatch_bound_session_send (const actor_ref_t &actor_ref,
                                                std::string packet_name,
                                                const zlink::message_t &payload) const;
    void on_join_spot (actor_gateway_state_t::join_spot_dispatcher_t dispatcher);
    void on_join_entry_spot (actor_gateway_state_t::join_entry_spot_dispatcher_t dispatcher);
    void on_relay (actor_gateway_state_t::relay_dispatcher_t dispatcher);
    void on_membership (actor_gateway_state_t::membership_query_t query);
    void on_disconnect (actor_gateway_state_t::disconnect_dispatcher_t dispatcher);
    void on_bound_session (actor_gateway_state_t::bound_session_registrar_t registrar);
    void bind_serializers (serializer_registry_t &serializers);
    void set_dispatch (dispatch_options_t options);

  private:
    std::shared_ptr<actor_gateway_state_t> _state;
};

} // namespace zlink::framework::detail
