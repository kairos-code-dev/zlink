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
class discovery_observer_t;
class socket_base_t;

struct provider_info_t
{
    uint16_t auto_connect_type;
    std::string channel_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
    uint16_t service_role;
    uint32_t weight;
    int64_t value;
    std::vector<unsigned char> metadata;
    uint64_t registered_at;
    uint32_t source_registry;
    uint64_t registration_id;
};

typedef std::pair<uint16_t, std::string> discovery_member_key_t;

class discovery_local_state_t
{
  public:
    discovery_local_state_t ();

    int set_route_value_max_size (size_t value_);
    int get_route_value_max_size (void *optval_, size_t *optvallen_) const;
    size_t route_value_max_size () const { return _route_value_max_size; }
    int set_spot_owner_sync_enabled (int value_);
    int get_spot_owner_sync_enabled (void *optval_, size_t *optvallen_) const;
    bool spot_owner_sync_enabled () const { return _spot_owner_sync_enabled; }
    void set_value (int64_t value_);
    int get_value (int64_t *value_out_) const;
    void snapshot_registration (int64_t *value_out_) const;

  private:
    int64_t _value;
    size_t _route_value_max_size;
    bool _spot_owner_sync_enabled;
};

struct discovery_service_change_t
{
    discovery_service_change_t ();

    bool changed;
    zlink_service_observation_event_t event;
};

class discovery_service_state_t
{
  public:
    discovery_service_state_t ();

    int add_observer (discovery_observer_t *observer_);
    int remove_observer (discovery_observer_t *observer_);
    bool has_inflight_observer_callbacks () const;
    void begin_observer_notification (
      const std::string &service_name_,
      const std::set<std::string> &services_,
      std::vector<discovery_observer_t *> *observers_out_);
    void finish_observer_notification (size_t observer_count_);
    void take_shutdown_observers (
      std::vector<discovery_observer_t *> *observers_out_);

    void snapshot_providers (std::vector<provider_info_t> *out_) const;
    void snapshot_member_peers (
      zlink_auto_connect_type_t auto_connect_type_,
      const std::set<discovery_member_key_t> &local_members_,
      std::vector<zlink_member_peer_entry_t> *out_) const;
    uint64_t update_seq () const;
    uint64_t service_update_seq () const;
    void apply_provider_snapshot (uint32_t registry_id_,
                                  uint64_t list_seq_,
                                  const std::vector<provider_info_t> &updated_,
                                  const std::string &channel_name_,
                                  const zlink_routing_id_t &routing_id_,
                                  discovery_service_change_t *change_out_);

  private:
    std::vector<provider_info_t> _providers;
    std::map<uint32_t, uint64_t> _registry_seq;
    std::set<discovery_observer_t *> _observers;
    size_t _observer_callbacks_inflight;
    uint64_t _update_seq;
    uint64_t _service_seq;
};

class discovery_bootstrap_socket_config_t
{
  public:
    discovery_bootstrap_socket_config_t ();

    void lock_routing_id (discovery_t *owner_);
    int set_routing_id (discovery_t *owner_, const void *data_, size_t size_);
    int routing_id (const discovery_t *owner_, zlink_routing_id_t *out_) const;
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

    const zlink_routing_id_t &routing_id_value () const { return _routing_id; }

  private:
    struct socket_opt_t
    {
        int option;
        std::vector<unsigned char> value;
    };

    void upsert_socket_option (int option_,
                               const void *optval_,
                               size_t optvallen_);

    std::vector<socket_opt_t> _socket_options;
    zlink_routing_id_t _routing_id;
    std::string _routing_id_override;
    bool _routing_id_locked;
};

class discovery_bootstrap_runtime_t
{
  public:
    discovery_bootstrap_runtime_t ();

    int connect_registry (discovery_t *owner_, const char *registry_endpoint_);
    int set_routing_id (discovery_t *owner_, const void *data_, size_t size_);
    int routing_id (const discovery_t *owner_, zlink_routing_id_t *out_) const;
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
    bool has_bootstrap_activity_locked () const;
    void apply_socket_option_to_bootstrap_dealers (
      discovery_t *owner_, int option_, const void *optval_, size_t optvallen_)
      const;
    void remember_bootstrap_endpoint (discovery_t *owner_,
                                      const char *registry_endpoint_);
    bool bootstrap_completed (discovery_t *owner_,
                              const char *registry_endpoint_) const;
    void begin_bootstrap_request (discovery_t *owner_,
                                  const std::string &registry_endpoint_);
    bool reset_bootstrap_request_if_timed_out (
      discovery_t *owner_,
      const std::string &registry_endpoint_,
      uint64_t now_ms_);
    void commit_bootstrap_success (discovery_t *owner_,
                                   const std::string &registry_endpoint_,
                                   const std::string &pub_endpoint_,
                                   const std::string &uplink_endpoint_,
                                   uint32_t heartbeat_interval_ms_,
                                   socket_base_t **adopted_report_dealer_out_);
    void collect_pending_bootstrap_endpoints (
      discovery_t *owner_, std::vector<std::string> *out_) const;
    void collect_registry_pub_endpoints (
      discovery_t *owner_, std::set<std::string> *out_) const;
    void take_shutdown_state (
      discovery_t *owner_,
      std::vector<std::pair<std::string, socket_base_t *> > *bootstrap_dealers);

    const zlink_routing_id_t &routing_id_value () const
    {
        return _socket_config.routing_id_value ();
    }
    uint32_t heartbeat_interval_ms () const
    {
        return _request_state.heartbeat_interval_ms;
    }

  private:
    friend class discovery_bootstrap_socket_config_t;

    struct bootstrap_state_t
    {
        socket_base_t *dealer;
        bool request_sent;
        uint64_t request_started_ms;

        bootstrap_state_t ();
    };

    struct request_state_t
    {
        request_state_t () : heartbeat_interval_ms (5000) {}

        std::set<std::string> registry_bootstrap_endpoints;
        std::set<std::string> bootstrapped_registry_endpoints;
        std::map<std::string, bootstrap_state_t> bootstrap_states;
        std::set<std::string> registry_pub_endpoints;
        uint32_t heartbeat_interval_ms;
    };

    discovery_bootstrap_socket_config_t _socket_config;
    request_state_t _request_state;
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
    void refresh_registered_service_heartbeats (discovery_t *owner_,
                                                uint64_t now_ms_);
    void remember_registry_uplink (discovery_t *owner_,
                                   const std::string &uplink_endpoint_);
    void adopt_report_dealer (discovery_t *owner_,
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
    struct socket_owner_state_t
    {
        std::set<std::string> registry_uplink_endpoints;
        std::string latest_registry_uplink_endpoint;
        std::map<std::string, socket_base_t *> report_dealers;
        std::map<std::string, socket_base_t *> control_dealers;
    };

    socket_base_t *create_uplink_dealer (discovery_t *owner_,
                                         const std::string &uplink_endpoint_,
                                         int linger_,
                                         int sndtimeo_ms_,
                                         int rcvtimeo_ms_,
	                                         bool use_bootstrap_routing_id_);
    socket_owner_state_t _socket_owner_state;
};
}

#endif
