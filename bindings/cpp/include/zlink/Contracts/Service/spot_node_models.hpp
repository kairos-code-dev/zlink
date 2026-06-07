/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "registry_models.hpp"
#include "actor_models.hpp"
#include "../Core/routing_id.hpp"
#include "../Eventing/status.hpp"


namespace zlink
{

namespace detail
{
struct service_model_access_t;
} // namespace detail

enum class spot_socket_role : int
{
    pub = 1,
    sub = 2
};

enum class spot_node_socket_type_t : int
{
    pair = 4097,
    dealer = 4100,
    router = 4101,
    stream = 4104,
    pub = 4098,
    xpub = 4102,
    sub = 4099,
    xsub = 4103
};

/// @brief The overall readiness state of a spot node.
enum class spot_node_state : int
{
    idle = 1, ///< Not yet connecting to any peer.
    connecting = 2,
    partial_ready = 3, ///< Some but not all peers are connected.
    ready = 4,         ///< All expected peers are connected.
    error = 5
};

/// @brief Which messaging patterns a spot node enables.
enum class spot_node_mode : int
{
    pubsub = 1, ///< Publish/subscribe only.
    routed = 2, ///< Routed request/reply only.
    all = 3     ///< Both pub/sub and routed.
};

/// @brief Which component owns a spot node socket.
enum class spot_node_socket_owner : int
{
    any = 0,  ///< Any owner (no filter).
    node = 1, ///< Owned by the node itself.
    spot = 2  ///< Owned by a spot.
};

/// @brief How a spot peer became known to the node.
enum class spot_peer_source : int
{
    manual = 1,    ///< Added manually by the application.
    discovery = 2, ///< Learned from a discovery service.
    mixed = 3      ///< Both manually added and discovered.
};

/// @brief The connection style of a spot peer.
enum class spot_peer_kind : int
{
    spot_mesh = 1,     ///< A peer in the spot mesh.
    router_channel = 2 ///< A peer reached over a router channel.
};

/// @brief The connection state of a spot peer.
enum class spot_peer_state : int
{
    configured = 1, ///< Configured but not yet connecting.
    connecting = 2,
    connected = 3
};

template <size_t N> inline std::string fixed_string_to_string (const char (&src_)[N]);

/// @brief How a subscription subject is matched.
enum class subject_kind : uint32_t
{
    none = 0,   ///< No subject.
    topic = 1,  ///< An exact topic match.
    pattern = 2 ///< A pattern match.
};

using spot_role_t = spot_socket_role;
using spot_node_state_t = spot_node_state;
using spot_node_mode_t = spot_node_mode;
using spot_node_socket_owner_t = spot_node_socket_owner;
using spot_peer_source_t = spot_peer_source;
using spot_peer_kind_t = spot_peer_kind;
using spot_peer_state_t = spot_peer_state;
using subject_kind_t = subject_kind;

/// @brief A snapshot of a spot node's status and peer/subject counts.
class spot_node_status_t
{
  public:
    spot_node_status_t () :
        channel_name_ (),
        local_endpoint_ (),
        node_routing_id_ (std::nullopt),
        state_ (spot_node_state::idle),
        configured_peer_count_ (0),
        active_peer_count_ (0),
        connected_peer_count_ (0),
        subject_count_ (0),
        ready_subject_count_ (0),
        disconnected_sub_target_count_ (0),
        disconnected_routed_target_count_ (0),
        last_error_ (0),
        last_changed_ms_ (0)
    {
    }

    const std::string &channel_name () const noexcept { return channel_name_; }

    const std::string &local_endpoint () const noexcept { return local_endpoint_; }

    const std::optional<routing_id_t> &node_routing_id () const noexcept
    {
        return node_routing_id_;
    }

    spot_node_state_t state () const noexcept { return state_; }

    uint32_t configured_peer_count () const noexcept { return configured_peer_count_; }

    uint32_t active_peer_count () const noexcept { return active_peer_count_; }

    uint32_t connected_peer_count () const noexcept { return connected_peer_count_; }

    uint32_t subject_count () const noexcept { return subject_count_; }

    uint32_t ready_subject_count () const noexcept { return ready_subject_count_; }

    uint32_t disconnected_sub_target_count () const noexcept
    {
        return disconnected_sub_target_count_;
    }

    uint32_t disconnected_routed_target_count () const noexcept
    {
        return disconnected_routed_target_count_;
    }

    int32_t last_error () const noexcept { return last_error_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    std::string channel_name_;
    std::string local_endpoint_;
    std::optional<routing_id_t> node_routing_id_;
    spot_node_state state_;
    uint32_t configured_peer_count_;
    uint32_t active_peer_count_;
    uint32_t connected_peer_count_;
    uint32_t subject_count_;
    uint32_t ready_subject_count_;
    uint32_t disconnected_sub_target_count_;
    uint32_t disconnected_routed_target_count_;
    int32_t last_error_;
    uint64_t last_changed_ms_;
    friend struct detail::service_model_access_t;
};

/// @brief One peer of a spot node and its connection details.
class spot_node_peer_entry_t
{
  public:
    spot_node_peer_entry_t () :
        channel_name_ (),
        local_endpoint_ (),
        peer_endpoint_ (),
        source_ (spot_peer_source::manual),
        kind_ (spot_peer_kind::spot_mesh),
        state_ (spot_peer_state::configured),
        weight_ (0),
        connected_since_ms_ (0),
        last_changed_ms_ (0)
    {
    }

    const std::string &channel_name () const noexcept { return channel_name_; }

    const std::string &local_endpoint () const noexcept { return local_endpoint_; }

    const std::string &peer_endpoint () const noexcept { return peer_endpoint_; }

    spot_peer_source_t source () const noexcept { return source_; }

    spot_peer_kind_t kind () const noexcept { return kind_; }

    spot_peer_state_t state () const noexcept { return state_; }

    peer_weight_t weight () const noexcept { return peer_weight_t::value (weight_); }

    std::chrono::milliseconds connected_since () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (connected_since_ms_));
    }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    std::string channel_name_;
    std::string local_endpoint_;
    std::string peer_endpoint_;
    spot_peer_source source_;
    spot_peer_kind kind_;
    spot_peer_state state_;
    uint32_t weight_;
    uint64_t connected_since_ms_;
    uint64_t last_changed_ms_;
    friend struct detail::service_model_access_t;
};

/// @brief Filters a spot node peer query; unset fields match anything.
class spot_node_peer_filter_t
{
  public:
    spot_node_peer_filter_t () = default;

    spot_node_peer_filter_t &peer_endpoint (std::string value_)
    {
        peer_endpoint_ = std::move (value_);
        return *this;
    }

    spot_node_peer_filter_t &source (spot_peer_source_t value_)
    {
        source_ = value_;
        return *this;
    }

    spot_node_peer_filter_t &state (spot_peer_state_t value_)
    {
        state_ = value_;
        return *this;
    }

    const std::optional<std::string> &peer_endpoint () const noexcept { return peer_endpoint_; }

    const std::optional<spot_peer_source_t> &source () const noexcept { return source_; }

    const std::optional<spot_peer_state_t> &state () const noexcept { return state_; }

  private:
    std::optional<std::string> peer_endpoint_;
    std::optional<spot_peer_source_t> source_;
    std::optional<spot_peer_state_t> state_;
};

/// @brief One subject (topic or pattern) served by a spot node.
class spot_node_subject_entry_t
{
  public:
    spot_node_subject_entry_t () :
        role_ (spot_socket_role::pub),
        subject_ (),
        subject_kind_ (zlink::subject_kind::none),
        ready_peer_count_ (0),
        active_peer_count_ (0),
        last_changed_ms_ (0)
    {
    }

    spot_role_t role () const noexcept { return role_; }

    const std::string &subject () const noexcept { return subject_; }

    subject_kind_t subject_kind () const noexcept { return subject_kind_; }

    uint32_t ready_peer_count () const noexcept { return ready_peer_count_; }

    uint32_t active_peer_count () const noexcept { return active_peer_count_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    spot_socket_role role_;
    std::string subject_;
    zlink::subject_kind subject_kind_;
    uint32_t ready_peer_count_;
    uint32_t active_peer_count_;
    uint64_t last_changed_ms_;
    friend struct detail::service_model_access_t;
};

/// @brief Filters a spot node subject query; unset fields match anything.
class spot_node_subject_filter_t
{
  public:
    spot_node_subject_filter_t () = default;

    spot_node_subject_filter_t &role (spot_role_t value_)
    {
        role_ = value_;
        return *this;
    }

    spot_node_subject_filter_t &subject (std::string value_)
    {
        subject_ = std::move (value_);
        return *this;
    }

    spot_node_subject_filter_t &subject_kind (subject_kind_t value_)
    {
        subject_kind_ = value_;
        return *this;
    }

    const std::optional<spot_role_t> &role () const noexcept { return role_; }

    const std::optional<std::string> &subject () const noexcept { return subject_; }

    const std::optional<subject_kind_t> &subject_kind () const noexcept { return subject_kind_; }

  private:
    std::optional<spot_role_t> role_;
    std::optional<std::string> subject_;
    std::optional<subject_kind_t> subject_kind_;
};

/// @brief Filters a spot node socket query; unset fields match anything.
class spot_node_socket_filter_t
{
  public:
    spot_node_socket_filter_t () = default;

    spot_node_socket_filter_t &owner (spot_node_socket_owner_t value_)
    {
        owner_ = value_;
        return *this;
    }

    spot_node_socket_filter_t &socket_type (spot_node_socket_type_t value_)
    {
        socket_type_ = value_;
        return *this;
    }

    spot_node_socket_filter_t &socket_name (std::string value_)
    {
        socket_name_ = std::move (value_);
        return *this;
    }

    const std::optional<spot_node_socket_owner_t> &owner () const noexcept { return owner_; }

    const std::optional<spot_node_socket_type_t> &socket_type () const noexcept
    {
        return socket_type_;
    }

    const std::optional<std::string> &socket_name () const noexcept { return socket_name_; }

  private:
    std::optional<spot_node_socket_owner_t> owner_;
    std::optional<spot_node_socket_type_t> socket_type_;
    std::optional<std::string> socket_name_;
};

/// @brief One socket owned within a spot node and its monitored status.
class spot_node_socket_entry_t
{
  public:
    spot_node_socket_entry_t () :
        owner_ (spot_node_socket_owner::any),
        owner_id_ (0),
        owner_name_ (),
        socket_name_ (),
        socket_type_ (spot_node_socket_type_t::pair),
        auto_hwm_visible_ (false),
        monitor_status_ ()
    {
    }

    spot_node_socket_owner_t owner () const noexcept { return owner_; }

    uint64_t owner_id () const noexcept { return owner_id_; }

    const std::string &owner_name () const noexcept { return owner_name_; }

    const std::string &socket_name () const noexcept { return socket_name_; }

    spot_node_socket_type_t socket_type () const noexcept { return socket_type_; }

    bool auto_hwm_visible () const noexcept { return auto_hwm_visible_; }

    const monitor_status_t &monitor_status () const noexcept { return monitor_status_; }

  private:
    spot_node_socket_owner owner_;
    uint64_t owner_id_;
    std::string owner_name_;
    std::string socket_name_;
    spot_node_socket_type_t socket_type_;
    bool auto_hwm_visible_;
    monitor_status_t monitor_status_;
    friend struct detail::service_model_access_t;
};

/// @brief One spot hosted on a spot node and its actor counts.
class spot_node_spot_entry_t
{
  public:
    spot_node_spot_entry_t () :
        spot_rid_ (detail::unchecked_empty_routing_id ()),
        spot_kind_ (spot_kind::invalid),
        dispatch_handler_attached_ (false),
        joined_actor_count_ (0),
        pending_actor_join_count_ (0),
        route_synced_ (false),
        last_changed_ms_ (0)
    {
    }

    const routing_id_t &spot_rid () const noexcept { return spot_rid_; }

    spot_kind kind () const noexcept { return spot_kind_; }

    bool dispatch_handler_attached () const noexcept { return dispatch_handler_attached_; }

    uint32_t joined_actor_count () const noexcept { return joined_actor_count_; }

    uint32_t pending_actor_join_count () const noexcept { return pending_actor_join_count_; }

    bool route_synced () const noexcept { return route_synced_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    routing_id_t spot_rid_;
    spot_kind spot_kind_;
    bool dispatch_handler_attached_;
    uint32_t joined_actor_count_;
    uint32_t pending_actor_join_count_;
    bool route_synced_;
    uint64_t last_changed_ms_;
    friend struct detail::service_model_access_t;
};

/// @brief One actor hosted on a spot node and its current placement.
class spot_node_actor_entry_t
{
  public:
    spot_node_actor_entry_t () :
        actor_ (),
        current_spot_rid_ (std::nullopt),
        current_spot_kind_ (spot_kind::invalid),
        route_synced_ (false),
        pending_message_count_ (0),
        last_changed_ms_ (0)
    {
    }

    const actor_ref_t &actor () const noexcept { return actor_; }

    const std::optional<routing_id_t> &current_spot_rid () const noexcept
    {
        return current_spot_rid_;
    }

    spot_kind current_spot_kind () const noexcept { return current_spot_kind_; }

    bool route_synced () const noexcept { return route_synced_; }

    uint32_t pending_message_count () const noexcept { return pending_message_count_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    actor_ref_t actor_;
    std::optional<routing_id_t> current_spot_rid_;
    spot_kind current_spot_kind_;
    bool route_synced_;
    uint32_t pending_message_count_;
    uint64_t last_changed_ms_;
    friend struct detail::service_model_access_t;
};

struct spot_service_attachment_stats_t
{
    std::string channel_name;
    uint32_t router_count = 0;
    uint32_t pub_count = 0;
    uint32_t sub_count = 0;
    uint32_t auto_router_count = 0;
    uint32_t auto_pub_count = 0;
    uint32_t auto_sub_count = 0;
};
} // namespace zlink
