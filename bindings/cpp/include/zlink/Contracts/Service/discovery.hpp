/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/context.hpp"
#include "../Messaging/message.hpp"
#include "../Core/routing_id.hpp"
#include "discovery_models.hpp"
#include "registry_models.hpp"
#include "actor_models.hpp"

#include <memory>

namespace zlink
{
namespace service
{
class discovery_t;
} // namespace service
namespace detail
{
struct discovery_access_t;
} // namespace detail
namespace service
{

/**
 * @brief Learns peer routes from a registry and resolves spots and actors for a fixed channel.
 * @note The caller owns this resource and must destroy it when done.
 */
class discovery_t
{
  public:
    discovery_t (context_t &ctx_,
                 auto_connect_type auto_connect_type_,
                 const std::string &channel_name_);

    discovery_t (context_t &ctx_,
                 const std::string &channel_name_,
                 auto_connect_type_t auto_connect_type_,
                 service_role_t)
        : discovery_t (ctx_, auto_connect_type_, channel_name_)
    {
    }

    ~discovery_t ();

    discovery_t (discovery_t &&other) noexcept;

    discovery_t &operator= (discovery_t &&other) noexcept;

    discovery_t (const discovery_t &) = delete;
    discovery_t &operator= (const discovery_t &) = delete;

    bool valid () const noexcept;

    void connect_registry (const std::string &endpoint_);

    void set_tls_client (const std::string &ca_cert_,
                         const std::string &hostname_ = std::string (),
                         bool trust_system_ = false);

    void set_value (int64_t value_);

    void get_value (int64_t *value_out_) const;

    int64_t value () const
    {
        int64_t result = 0;
        get_value (&result);
        return result;
    }

    void set_spot_owner_sync_enabled (bool enabled_);

    bool spot_owner_sync_enabled () const;

    void set_actor_route_sync_enabled (bool enabled_);

    bool actor_route_sync_enabled () const;

    std::vector<member_peer_entry_t> member_peers () const;

    spot_route_t resolve_spot (const routing_id_t &spot_rid_);

    actor_route_t resolve_actor (const std::string &actor_id_);

    void close ();

  private:
    friend struct zlink::detail::discovery_access_t;

    struct impl;
    std::unique_ptr<impl> _impl;
    int _last_error;
};

} // namespace service
} // namespace zlink
