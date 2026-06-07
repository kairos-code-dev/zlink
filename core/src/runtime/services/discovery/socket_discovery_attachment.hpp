/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_SOCKET_DISCOVERY_ATTACHMENT_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_SOCKET_DISCOVERY_ATTACHMENT_HPP_INCLUDED__

#include <zlink.h>

#include "services/discovery/discovery_observer.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <set>
#include <string>

namespace zlink
{
class socket_base_t;

class socket_discovery_attachment_t : public discovery_observer_t
{
  public:
    explicit socket_discovery_attachment_t (socket_base_t *socket_);
    ~socket_discovery_attachment_t () ZLINK_OVERRIDE;

    int attach (discovery_t *discovery_);
    int on_public_bind_begin (const char *endpoint_);
    int on_bind_success (const std::string &endpoint_);
    int on_public_connect () const;
    int on_public_term_endpoint () const;
    int on_public_disconnect_rid () const;
    int on_public_close () const;
    void on_local_peer_weight_changed ();

    void on_service_update (const std::string &channel_name_) ZLINK_OVERRIDE;
    void on_discovery_shutdown_requested (discovery_t *discovery_) ZLINK_OVERRIDE;
    void on_discovery_destroyed (discovery_t *discovery_) ZLINK_OVERRIDE;

  private:
    int attach_validation (discovery_t *discovery_,
                           uint16_t *local_role_out_,
                           std::string *bound_endpoint_out_);
    int register_bound_endpoint (discovery_t *discovery_,
                                 uint16_t local_role_,
                                 const std::string &endpoint_,
                                 std::string *resolved_endpoint_out_);
    bool ensure_socket_routing_id (zlink_routing_id_t *out_);
    void report_topology (discovery_t *discovery_,
                          uint16_t local_role_,
                          const std::string &advertise_endpoint_,
                          uint16_t state_,
                          uint32_t desired_count_,
                          uint32_t ready_count_,
                          int error_code_,
                          bool flush_immediately_);
    void refresh_peers (discovery_t *discovery_,
                        uint16_t local_role_,
                        const std::string &advertise_endpoint_,
                        bool shutdown_requested_);
    bool public_api_forbidden (int *errno_out_) const;

    socket_base_t *_socket;
    mutable mutex_t _sync;
    discovery_t *_discovery;
    uint16_t _local_role;
    std::string _advertise_endpoint;
    bool _registered;
    bool _shutdown_requested;
    uint64_t _refresh_seq;
    std::set<std::string> _discovery_managed_peer_endpoints;
    std::set<std::string> _active_peer_endpoints;
    std::map<std::string, std::string> _discovery_managed_peers_by_rid;
    std::map<std::string, std::string> _active_peers_by_rid;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (socket_discovery_attachment_t)
};
}

#endif
