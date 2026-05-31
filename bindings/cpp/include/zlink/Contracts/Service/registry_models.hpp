/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "discovery_models.hpp"
#include "../Core/routing_id.hpp"


namespace zlink
{

namespace detail
{
struct service_model_access_t;
} // namespace detail

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

/// @brief The operational state of a registry.
enum class registry_state : int
{
    idle = 1,
    active = 2,
    degraded = 3,
    error = 4
};

/// @brief Where a topology entry was learned from.
enum class topology_source : int
{
    manual = 1,    ///< Added manually by the application.
    discovery = 2, ///< Learned from a discovery service.
    registry = 3   ///< Learned from a service registry.
};

/// @brief The lifecycle state of a topology connection.
enum class topology_state : int
{
    discovered = 1, ///< The peer was found but a connection is not yet established.
    connecting = 2,
    ready = 3,      ///< The peer is connected and usable.
    lost = 4,
    error = 5,
    stopped = 6     ///< The connection was explicitly stopped.
};

using registry_socket_role_t = registry_socket_role;
using discovery_socket_role_t = discovery_socket_role;
using registry_state_t = registry_state;
using topology_source_t = topology_source;
using topology_state_t = topology_state;

/// @brief One member peer registered on a channel.
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
