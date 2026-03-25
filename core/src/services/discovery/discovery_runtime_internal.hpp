/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_RUNTIME_INTERNAL_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_RUNTIME_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace zlink
{
class discovery_t;
class socket_base_t;

class discovery_bootstrap_runtime_t
{
  public:
    discovery_bootstrap_runtime_t ();

    int connect_registry (discovery_t *owner_, const char *registry_endpoint_);
    int set_routing_id (discovery_t *owner_, const void *data_, size_t size_);
    int routing_id (discovery_t *owner_, zlink_routing_id_t *out_) const;
    int set_option (discovery_t *owner_,
                    int option_,
                    const void *optval_,
                    size_t optvallen_);
    int set_tls_client (discovery_t *owner_,
                        const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    void apply_socket_options (socket_base_t *socket_) const;
    void apply_socket_option_to_existing (discovery_t *owner_,
                                          int option_,
                                          const void *optval_,
                                          size_t optvallen_) const;
    bool ensure_socket_routing_id (socket_base_t *socket_);
    int bootstrap_registry (discovery_t *owner_, const char *registry_endpoint_);
    int ensure_bootstrap_dealer (discovery_t *owner_,
                                 const std::string &registry_endpoint_,
                                 socket_base_t **dealer_out_);
    void collect_pending_bootstrap_endpoints (
      discovery_t *owner_, std::vector<std::string> *out_) const;
    void collect_registry_pub_endpoints (
      discovery_t *owner_, std::set<std::string> *out_) const;
    void take_shutdown_state (
      discovery_t *owner_,
      std::vector<std::pair<std::string, socket_base_t *> > *bootstrap_dealers);

    const zlink_routing_id_t &routing_id_value () const { return _routing_id; }
    uint32_t heartbeat_interval_ms () const { return _heartbeat_interval_ms; }

  private:
    struct socket_opt_t
    {
        int option;
        std::vector<unsigned char> value;
    };

    struct bootstrap_state_t
    {
        socket_base_t *dealer;
        bool request_sent;
        uint64_t request_started_ms;

        bootstrap_state_t ();
    };

    std::vector<socket_opt_t> _socket_options;
    std::set<std::string> _registry_bootstrap_endpoints;
    std::set<std::string> _bootstrapped_registry_endpoints;
    std::map<std::string, bootstrap_state_t> _bootstrap_states;
    std::set<std::string> _registry_pub_endpoints;
    zlink_routing_id_t _routing_id;
    std::string _routing_id_override;
    bool _routing_id_locked;
    uint32_t _heartbeat_interval_ms;
};

class discovery_uplink_runtime_t
{
  public:
    discovery_uplink_runtime_t ();

    void apply_socket_option_to_existing (discovery_t *owner_,
                                          int option_,
                                          const void *optval_,
                                          size_t optvallen_) const;
    int ensure_topology_reporter (discovery_t *owner_,
                                  const std::string &uplink_endpoint_,
                                  socket_base_t **dealer_out_);
    int ensure_control_dealer (discovery_t *owner_,
                               const std::string &uplink_endpoint_,
                               socket_base_t **dealer_out_);
    int ensure_topology_reporters (discovery_t *owner_);
    void flush_topology_reports (discovery_t *owner_);
    void flush_gateway_peer_reports (discovery_t *owner_);
    void refresh_registered_service_heartbeats (discovery_t *owner_,
                                                uint64_t now_ms_);
    void remember_bootstrap_success (discovery_t *owner_,
                                     const std::string &registry_endpoint_,
                                     const std::string &uplink_endpoint_,
                                     socket_base_t *bootstrap_dealer_);
    void collect_uplink_endpoints (discovery_t *owner_,
                                   std::vector<std::string> *out_) const;
    bool latest_registry_uplink (discovery_t *owner_, std::string *out_) const;
    void take_shutdown_state (
      discovery_t *owner_,
      std::vector<std::pair<std::string, socket_base_t *> > *report_dealers,
      std::vector<std::pair<std::string, socket_base_t *> > *control_dealers);

  private:
    std::set<std::string> _registry_uplink_endpoints;
    std::string _latest_registry_uplink_endpoint;
    std::map<std::string, socket_base_t *> _report_dealers;
    std::map<std::string, socket_base_t *> _control_dealers;
};
}

#endif
