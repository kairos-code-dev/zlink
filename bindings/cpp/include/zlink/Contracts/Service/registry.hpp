/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/context.hpp"
#include "../Messaging/message.hpp"
#include "../Core/routing_id.hpp"
#include "models.hpp"

#include <memory>

namespace zlink
{
namespace service
{

class registry_t;

} // namespace service

namespace detail
{
struct registry_access_t;
} // namespace detail

namespace service
{

class registry_t
{
  public:
    explicit registry_t (context_t &ctx_);

    ~registry_t ();

    registry_t (registry_t &&other) noexcept;

    registry_t &operator= (registry_t &&other) noexcept;

    registry_t (const registry_t &) = delete;
    registry_t &operator= (const registry_t &) = delete;

    bool valid () const noexcept;

    void bind (const std::string &pub_endpoint_, const std::string &router_endpoint_);

    void set_id (uint32_t registry_id_)
    {
        set (14337, registry_id_);
    }

    void set (int option_, uint32_t value_);

    uint32_t get (int option_) const;

    void add_peer (const std::string &peer_pub_endpoint_);

    void set_heartbeat (uint32_t interval_ms_, uint32_t timeout_ms_)
    {
        set (14338, interval_ms_);
        set (14339, timeout_ms_);
    }

    void set_heartbeat (std::chrono::milliseconds interval_,
                        std::chrono::milliseconds timeout_)
    {
        set_heartbeat (static_cast<uint32_t> (interval_.count ()),
                       static_cast<uint32_t> (timeout_.count ()));
    }

    void set_broadcast_interval (uint32_t interval_ms_)
    {
        set (14340, interval_ms_);
    }

    void set_broadcast_interval (std::chrono::milliseconds interval_)
    {
        set_broadcast_interval (static_cast<uint32_t> (interval_.count ()));
    }

    void set_tls_server (const std::string &cert_,
                         const std::string &key_,
                         bool require_client_cert_ = false);

    void set_tls_client (const std::string &ca_cert_,
                         const std::string &hostname_ = std::string (),
                         bool trust_system_ = false);

    registry_status_t status () const;

    std::vector<registry_service_summary_entry_t>
    service_summary (
      const registry_service_summary_filter_t *filter_ = NULL) const;

    std::vector<registry_service_summary_entry_t>
    service_summary (
      const registry_service_summary_filter_t &filter_) const
    {
        return service_summary (&filter_);
    }

    std::vector<registry_topology_entry_t> topology () const;

    std::vector<registry_topology_entry_t>
    topology (const registry_topology_filter_t &filter_) const;

    std::vector<member_peer_entry_t>
    member_peers (const std::string &channel_name_) const;

    void close ();

  private:
    friend struct zlink::detail::registry_access_t;

    struct impl;
    std::unique_ptr<impl> _impl;
    int _last_error;
};

class registry_query_client_t
{
  public:
    explicit registry_query_client_t (context_t &ctx_);

    ~registry_query_client_t ();

    registry_query_client_t (registry_query_client_t &&other) noexcept;

    registry_query_client_t &
    operator= (registry_query_client_t &&other) noexcept;

    registry_query_client_t (const registry_query_client_t &) = delete;
    registry_query_client_t &
    operator= (const registry_query_client_t &) = delete;

    bool valid () const noexcept;

    void connect (const std::string &endpoint_);

    std::vector<registry_topology_entry_t>
    topology (const registry_topology_filter_t *filter_ = NULL) const;

    void close ();

  private:
    friend struct zlink::detail::registry_access_t;

    struct impl;
    std::unique_ptr<impl> _impl;
    int _last_error;
};

} // namespace service

} // namespace zlink
