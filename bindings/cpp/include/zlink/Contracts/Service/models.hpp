/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Eventing/status.hpp"
#include "actor_models.hpp"

namespace zlink
{

namespace detail
{
struct service_model_access_t;
} // namespace detail

enum class auto_connect_type : int
{
    invalid = 0,
    route_mesh = 1,
    client_server = 2,
    dealer_mesh = 3,
    fanout = 4,
    spot_mesh = 5
};

enum class service_role : int
{
    invalid = 0,
    spot = 2,
    router = 3,
    dealer = 4,
    pub = 5,
    sub = 6
};

enum class service_kind : int
{
    discovery = 1,
    spot_sub = 3,
    spot_pub = 4,
    socket = 5
};

enum class registry_socket_role : int
{
    pub = 1,
    router = 2,
    peer_sub = 3
};

enum class discovery_socket_role : int
{
    sub = 0
};

enum class spot_node_socket_role : int
{
    node = 0,
    pub = 1,
    sub = 2,
    dealer = 3
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

enum class spot_socket_role : int
{
    pub = 1,
    sub = 2
};

enum class spot_node_state : int
{
    idle = 1,
    connecting = 2,
    partial_ready = 3,
    ready = 4,
    error = 5
};

enum class spot_node_mode : int
{
    pubsub = 1,
    routed = 2,
    all = 3
};

enum class spot_node_socket_owner : int
{
    any = 0,
    node = 1,
    spot = 2
};

enum class spot_peer_source : int
{
    manual = 1,
    discovery = 2,
    mixed = 3
};

enum class spot_peer_kind : int
{
    spot_mesh = 1,
    router_channel = 2
};

enum class spot_peer_state : int
{
    configured = 1,
    connecting = 2,
    connected = 3
};

enum class registry_state : int
{
    idle = 1,
    active = 2,
    degraded = 3,
    error = 4
};

enum class topology_source : int
{
    manual = 1,
    discovery = 2,
    registry = 3
};

enum class topology_state : int
{
    discovered = 1,
    connecting = 2,
    ready = 3,
    lost = 4,
    error = 5,
    stopped = 6
};

template<size_t N> inline std::string fixed_string_to_string (const char (&src_)[N]);

enum class subject_kind : uint32_t
{
    none = 0,
    topic = 1,
    pattern = 2
};

using registry_socket_role_t = registry_socket_role;
using discovery_socket_role_t = discovery_socket_role;
using spot_node_socket_role_t = spot_node_socket_role;
using auto_connect_type_t = auto_connect_type;
using service_role_t = service_role;
using service_kind_t = service_kind;
using spot_role_t = spot_socket_role;
using spot_node_state_t = spot_node_state;
using spot_node_mode_t = spot_node_mode;
using spot_node_socket_owner_t = spot_node_socket_owner;
using spot_peer_source_t = spot_peer_source;
using spot_peer_kind_t = spot_peer_kind;
using spot_peer_state_t = spot_peer_state;
using registry_state_t = registry_state;
using topology_source_t = topology_source;
using topology_state_t = topology_state;
using subject_kind_t = subject_kind;

class member_peer_entry_t
{
  public:
    member_peer_entry_t ()
        : auto_connect_type_ (zlink::auto_connect_type::invalid),
          service_role_ (service_role::invalid), channel_name_ (), endpoint_ (),
          routing_id_ (std::nullopt), weight_ (0),
          value_ (0)
    {
    }

    auto_connect_type_t auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    service_role_t service_role () const noexcept { return service_role_; }

    const std::string &channel_name () const noexcept { return channel_name_; }

    const std::string &endpoint () const noexcept { return endpoint_; }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return routing_id_;
    }

    peer_weight_t weight () const noexcept { return peer_weight_t::value (weight_); }

    int64_t value () const noexcept { return value_; }

  private:
    zlink::auto_connect_type auto_connect_type_;
    zlink::service_role service_role_;
    std::string channel_name_;
    std::string endpoint_;
    std::optional<routing_id_t> routing_id_;
    uint32_t weight_;
    int64_t value_;
    friend struct detail::service_model_access_t;
};

class registry_topology_entry_t
{
  public:
    registry_topology_entry_t ()
        : auto_connect_type_ (zlink::auto_connect_type::invalid),
          routing_id_ (std::nullopt), service_kind_ (service_kind::socket),
          service_role_ (service_role::invalid), channel_name_ (), endpoint_ (),
          source_ (topology_source::manual), state_ (topology_state::discovered),
          desired_count_ (0), ready_count_ (0), error_code_ (0),
          last_reported_ms_ (0)
    {
    }

    auto_connect_type_t auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return routing_id_;
    }

    service_kind_t service_kind () const noexcept { return service_kind_; }

    service_role_t service_role () const noexcept { return service_role_; }

    const std::string &channel_name () const noexcept { return channel_name_; }

    const std::string &endpoint () const noexcept { return endpoint_; }

    topology_source_t source () const noexcept { return source_; }

    topology_state_t state () const noexcept { return state_; }

    uint32_t desired_count () const noexcept { return desired_count_; }

    uint32_t ready_count () const noexcept { return ready_count_; }

    uint32_t error_code () const noexcept { return error_code_; }

    std::chrono::milliseconds last_reported () const noexcept
    {
        return std::chrono::milliseconds (
          static_cast<int64_t> (last_reported_ms_));
    }

  private:
    zlink::auto_connect_type auto_connect_type_;
    std::optional<routing_id_t> routing_id_;
    zlink::service_kind service_kind_;
    zlink::service_role service_role_;
    std::string channel_name_;
    std::string endpoint_;
    topology_source source_;
    topology_state state_;
    uint32_t desired_count_;
    uint32_t ready_count_;
    uint32_t error_code_;
    uint64_t last_reported_ms_;
    friend struct detail::service_model_access_t;
};

class spot_node_status_t
{
  public:
    spot_node_status_t ()
        : channel_name_ (), local_endpoint_ (), node_routing_id_ (std::nullopt),
          state_ (spot_node_state::idle), configured_peer_count_ (0),
          active_peer_count_ (0), connected_peer_count_ (0), subject_count_ (0),
          ready_subject_count_ (0), disconnected_sub_target_count_ (0),
          disconnected_routed_target_count_ (0), last_error_ (0),
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

    uint32_t configured_peer_count () const noexcept
    {
        return configured_peer_count_;
    }

    uint32_t active_peer_count () const noexcept { return active_peer_count_; }

    uint32_t connected_peer_count () const noexcept
    {
        return connected_peer_count_;
    }

    uint32_t subject_count () const noexcept { return subject_count_; }

    uint32_t ready_subject_count () const noexcept
    {
        return ready_subject_count_;
    }

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

class registry_service_summary_entry_t
{
  public:
    registry_service_summary_entry_t ()
        : auto_connect_type_ (zlink::auto_connect_type::invalid),
          service_role_ (service_role::invalid), channel_name_ (),
          total_count_ (0), connecting_count_ (0), ready_count_ (0),
          error_count_ (0), stopped_count_ (0), last_reported_ms_ (0)
    {
    }

    auto_connect_type_t auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    service_role_t service_role () const noexcept { return service_role_; }

    const std::string &channel_name () const noexcept { return channel_name_; }

    uint32_t total_count () const noexcept { return total_count_; }

    uint32_t connecting_count () const noexcept { return connecting_count_; }

    uint32_t ready_count () const noexcept { return ready_count_; }

    uint32_t error_count () const noexcept { return error_count_; }

    uint32_t stopped_count () const noexcept { return stopped_count_; }

    std::chrono::milliseconds last_reported () const noexcept
    {
        return std::chrono::milliseconds (
          static_cast<int64_t> (last_reported_ms_));
    }

  private:
    zlink::auto_connect_type auto_connect_type_;
    zlink::service_role service_role_;
    std::string channel_name_;
    uint32_t total_count_;
    uint32_t connecting_count_;
    uint32_t ready_count_;
    uint32_t error_count_;
    uint32_t stopped_count_;
    uint64_t last_reported_ms_;
    friend struct detail::service_model_access_t;
};

class registry_service_summary_filter_t
{
  public:
    registry_service_summary_filter_t () = default;

    registry_service_summary_filter_t &
    auto_connect_type (auto_connect_type_t value_)
    {
        auto_connect_type_ = value_;
        return *this;
    }

    registry_service_summary_filter_t &service_role (service_role_t value_)
    {
        service_role_ = value_;
        return *this;
    }

    registry_service_summary_filter_t &channel_name (std::string value_)
    {
        channel_name_ = std::move (value_);
        return *this;
    }

    const std::optional<auto_connect_type_t> &auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    const std::optional<service_role_t> &service_role () const noexcept
    {
        return service_role_;
    }

    const std::optional<std::string> &channel_name () const noexcept
    {
        return channel_name_;
    }

  private:
    std::optional<auto_connect_type_t> auto_connect_type_;
    std::optional<service_role_t> service_role_;
    std::optional<std::string> channel_name_;
};

class registry_status_t
{
  public:
    registry_status_t ()
        : registry_id_ (0), bind_endpoint_ (), state_ (registry_state::idle),
          topology_entry_count_ (0), peer_registry_count_ (0),
          connected_peer_registry_count_ (0), list_seq_ (0), last_error_ (0),
          last_changed_ms_ (0)
    {
    }

    uint32_t registry_id () const noexcept { return registry_id_; }

    const std::string &bind_endpoint () const noexcept { return bind_endpoint_; }

    registry_state_t state () const noexcept { return state_; }

    uint32_t topology_entry_count () const noexcept
    {
        return topology_entry_count_;
    }

    uint32_t peer_registry_count () const noexcept
    {
        return peer_registry_count_;
    }

    uint32_t connected_peer_registry_count () const noexcept
    {
        return connected_peer_registry_count_;
    }

    uint64_t list_seq () const noexcept { return list_seq_; }

    int32_t last_error () const noexcept { return last_error_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    uint32_t registry_id_;
    std::string bind_endpoint_;
    registry_state state_;
    uint32_t topology_entry_count_;
    uint32_t peer_registry_count_;
    uint32_t connected_peer_registry_count_;
    uint64_t list_seq_;
    int32_t last_error_;
    uint64_t last_changed_ms_;
    friend struct detail::service_model_access_t;
};

class spot_node_peer_entry_t
{
  public:
    spot_node_peer_entry_t ()
        : channel_name_ (), local_endpoint_ (), peer_endpoint_ (),
          source_ (spot_peer_source::manual),
          kind_ (spot_peer_kind::spot_mesh),
          state_ (spot_peer_state::configured), weight_ (0),
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
        return std::chrono::milliseconds (
          static_cast<int64_t> (connected_since_ms_));
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

    const std::optional<std::string> &peer_endpoint () const noexcept
    {
        return peer_endpoint_;
    }

    const std::optional<spot_peer_source_t> &source () const noexcept
    {
        return source_;
    }

    const std::optional<spot_peer_state_t> &state () const noexcept
    {
        return state_;
    }

  private:
    std::optional<std::string> peer_endpoint_;
    std::optional<spot_peer_source_t> source_;
    std::optional<spot_peer_state_t> state_;
};

class spot_node_subject_entry_t
{
  public:
    spot_node_subject_entry_t ()
        : role_ (spot_socket_role::pub), subject_ (),
          subject_kind_ (zlink::subject_kind::none), ready_peer_count_ (0),
          active_peer_count_ (0), last_changed_ms_ (0)
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

    const std::optional<std::string> &subject () const noexcept
    {
        return subject_;
    }

    const std::optional<subject_kind_t> &subject_kind () const noexcept
    {
        return subject_kind_;
    }

  private:
    std::optional<spot_role_t> role_;
    std::optional<std::string> subject_;
    std::optional<subject_kind_t> subject_kind_;
};

class spot_node_socket_filter_t
{
  public:
    spot_node_socket_filter_t () = default;

    spot_node_socket_filter_t &owner (spot_node_socket_owner_t value_)
    {
        owner_ = value_;
        return *this;
    }

    spot_node_socket_filter_t &socket_type (
      spot_node_socket_type_t value_)
    {
        socket_type_ = value_;
        return *this;
    }

    spot_node_socket_filter_t &socket_name (std::string value_)
    {
        socket_name_ = std::move (value_);
        return *this;
    }

    const std::optional<spot_node_socket_owner_t> &owner () const noexcept
    {
        return owner_;
    }

    const std::optional<spot_node_socket_type_t> &socket_type () const noexcept
    {
        return socket_type_;
    }

    const std::optional<std::string> &socket_name () const noexcept
    {
        return socket_name_;
    }

  private:
    std::optional<spot_node_socket_owner_t> owner_;
    std::optional<spot_node_socket_type_t> socket_type_;
    std::optional<std::string> socket_name_;
};

class spot_node_socket_entry_t
{
  public:
    spot_node_socket_entry_t ()
        : owner_ (spot_node_socket_owner::any), owner_id_ (0), owner_name_ (),
          socket_name_ (), socket_type_ (spot_node_socket_type_t::pair),
          auto_hwm_visible_ (false), monitor_status_ ()
    {
    }

    spot_node_socket_owner_t owner () const noexcept { return owner_; }

    uint64_t owner_id () const noexcept { return owner_id_; }

    const std::string &owner_name () const noexcept { return owner_name_; }

    const std::string &socket_name () const noexcept { return socket_name_; }

    spot_node_socket_type_t socket_type () const noexcept
    {
        return socket_type_;
    }

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

class spot_node_spot_entry_t
{
  public:
    spot_node_spot_entry_t ()
        : spot_rid_ (detail::unchecked_empty_routing_id ()),
          spot_kind_ (spot_kind::invalid),
          dispatch_handler_attached_ (false),
          joined_actor_count_ (0), pending_actor_join_count_ (0),
          route_synced_ (false), last_changed_ms_ (0)
    {
    }

    const routing_id_t &spot_rid () const noexcept { return spot_rid_; }

    spot_kind kind () const noexcept { return spot_kind_; }

    bool dispatch_handler_attached () const noexcept
    {
        return dispatch_handler_attached_;
    }

    uint32_t joined_actor_count () const noexcept
    {
        return joined_actor_count_;
    }

    uint32_t pending_actor_join_count () const noexcept
    {
        return pending_actor_join_count_;
    }

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

class spot_node_actor_entry_t
{
  public:
    spot_node_actor_entry_t ()
        : actor_ (), current_spot_rid_ (std::nullopt),
          current_spot_kind_ (spot_kind::invalid),
          route_synced_ (false), pending_message_count_ (0),
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

    uint32_t pending_message_count () const noexcept
    {
        return pending_message_count_;
    }

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

enum class spot_service_attachment_role_t
{
    router = 1,
    pub = 2,
    sub = 3
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

class registry_topology_filter_t
{
  public:
    registry_topology_filter_t () = default;

    registry_topology_filter_t &auto_connect_type (auto_connect_type_t value_)
    {
        auto_connect_type_ = value_;
        return *this;
    }

    registry_topology_filter_t &service_kind (service_kind_t value_)
    {
        service_kind_ = value_;
        return *this;
    }

    registry_topology_filter_t &service_role (service_role_t value_)
    {
        service_role_ = value_;
        return *this;
    }

    registry_topology_filter_t &channel_name (std::string value_)
    {
        channel_name_ = std::move (value_);
        return *this;
    }

    registry_topology_filter_t &routing_id (routing_id_t value_)
    {
        routing_id_ = std::move (value_);
        return *this;
    }

    registry_topology_filter_t &state (topology_state_t value_)
    {
        state_ = value_;
        return *this;
    }

    registry_topology_filter_t &source (topology_source_t value_)
    {
        source_ = value_;
        return *this;
    }

    const std::optional<auto_connect_type_t> &auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    const std::optional<service_kind_t> &service_kind () const noexcept
    {
        return service_kind_;
    }

    const std::optional<service_role_t> &service_role () const noexcept
    {
        return service_role_;
    }

    const std::optional<std::string> &channel_name () const noexcept
    {
        return channel_name_;
    }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return routing_id_;
    }

    const std::optional<topology_state_t> &state () const noexcept
    {
        return state_;
    }

    const std::optional<topology_source_t> &source () const noexcept
    {
        return source_;
    }

  private:
    std::optional<auto_connect_type_t> auto_connect_type_;
    std::optional<service_kind_t> service_kind_;
    std::optional<service_role_t> service_role_;
    std::optional<std::string> channel_name_;
    std::optional<routing_id_t> routing_id_;
    std::optional<topology_state_t> state_;
    std::optional<topology_source_t> source_;
};

} // namespace zlink
