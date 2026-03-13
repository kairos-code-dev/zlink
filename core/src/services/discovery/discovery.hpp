/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_DISCOVERY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_DISCOVERY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "services/common/service_runtime_base.hpp"
#include "services/common/service_monitor.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class discovery_t;

enum discovery_socket_role_t
{
    discovery_socket_sub = 1
};

struct provider_info_t
{
    std::string service_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
    uint32_t weight;
    uint64_t registered_at;
};

class discovery_observer_t
{
  public:
    virtual ~discovery_observer_t () {}
    virtual void on_service_update (const std::string &service_name_) = 0;
    virtual void on_discovery_destroyed (discovery_t *discovery_)
    {
        (void) discovery_;
    }
};

class discovery_t
{
  public:
    discovery_t (ctx_t *ctx_, uint16_t service_type_);
    ~discovery_t ();

    bool check_tag () const;

    int connect_registry (const char *registry_endpoint_);
    int set_routing_id (const void *data_, size_t size_);
    int routing_id (zlink_routing_id_t *out_) const;
    int set_socket_option (int socket_role_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_);
    void *monitor_open (int events_);

    int destroy ();
    void set_discovery_summary_enabled (bool enabled_);
    int register_service (uint16_t service_type_,
                          const char *service_name_,
                          const char *endpoint_,
                          uint32_t weight_,
                          std::string *resolved_endpoint_out_,
                          const zlink_routing_id_t *routing_id_ = NULL);
    int update_service_weight (uint16_t service_type_,
                               const char *service_name_,
                               const char *endpoint_,
                               uint32_t weight_);
    int unregister_service (uint16_t service_type_,
                            const char *service_name_,
                            const char *endpoint_);

    uint16_t service_type () const { return _service_type; }

    void snapshot_providers (const std::string &service_name_,
                             std::vector<provider_info_t> *out_);
    bool latest_registry_uplink (std::string *out_);
    uint64_t update_seq ();
    uint64_t service_update_seq (const std::string &service_name_);
    void add_observer (discovery_observer_t *observer_);
    void remove_observer (discovery_observer_t *observer_);
    void upsert_service_summary (const zlink_registry_topology_entry_t &entry_);
    void upsert_gateway_peer_summary (
      const zlink_registry_gateway_peer_entry_t &entry_);
    void erase_service_summary (uint16_t service_kind_,
                                const zlink_routing_id_t &routing_id_,
                                const std::string &service_name_,
                                bool stopped_);

  private:
    struct service_state_t
    {
        std::vector<provider_info_t> providers;
    };

    static void control_task (void *arg_);
    void tick ();
    int ensure_sub_socket ();
    void close_sub_socket ();
    void apply_socket_options_locked (socket_base_t *socket_);
    void apply_socket_options_to_existing_locked (int option_,
                                                  const void *optval_,
                                                  size_t optvallen_);
    bool ensure_socket_routing_id (socket_base_t *socket_);
    int bootstrap_registry (const char *registry_endpoint_);
    int ensure_bootstrap_dealer (const std::string &registry_endpoint_,
                                 socket_base_t **dealer_out_);
    void handle_service_list (const std::vector<zlink_msg_t> &frames_);
    void notify_observers (const std::set<std::string> &services_);
    int ensure_topology_reporters ();
    int ensure_topology_reporter_locked (const std::string &uplink_endpoint_,
                                         socket_base_t **dealer_out_);
    int ensure_control_dealer_locked (const std::string &uplink_endpoint_,
                                      socket_base_t **dealer_out_);
    void flush_topology_reports ();
    void flush_gateway_peer_reports ();
    void refresh_registered_service_heartbeats (uint64_t now_ms_);

    struct topology_key_t
    {
        uint16_t service_kind;
        std::string routing_id_key;
        std::string service_name;

        bool operator< (const topology_key_t &other_) const
        {
            if (service_kind != other_.service_kind)
                return service_kind < other_.service_kind;
            if (routing_id_key != other_.routing_id_key)
                return routing_id_key < other_.routing_id_key;
            return service_name < other_.service_name;
        }
    };

    struct topology_summary_t
    {
        zlink_registry_topology_entry_t entry;
        bool dirty;
        bool tombstone;
    };

    struct gateway_peer_key_t
    {
        std::string gateway_routing_id_key;
        std::string service_name;
        std::string peer_routing_id_key;

        bool operator< (const gateway_peer_key_t &other_) const
        {
            if (gateway_routing_id_key != other_.gateway_routing_id_key)
                return gateway_routing_id_key < other_.gateway_routing_id_key;
            if (service_name != other_.service_name)
                return service_name < other_.service_name;
            return peer_routing_id_key < other_.peer_routing_id_key;
        }
    };

    struct gateway_peer_summary_t
    {
        zlink_registry_gateway_peer_entry_t entry;
        bool dirty;
    };

    struct bootstrap_state_t
    {
        socket_base_t *dealer;
        bool request_sent;
        uint64_t request_started_ms;

        bootstrap_state_t () :
            dealer (NULL),
            request_sent (false),
            request_started_ms (0)
        {
        }
    };

    struct registered_service_key_t
    {
        uint16_t service_type;
        std::string service_name;
        std::string endpoint;

        bool operator< (const registered_service_key_t &other_) const
        {
            if (service_type != other_.service_type)
                return service_type < other_.service_type;
            if (service_name != other_.service_name)
                return service_name < other_.service_name;
            return endpoint < other_.endpoint;
        }
    };

    struct registered_service_t
    {
        uint16_t service_type;
        std::string service_name;
        std::string endpoint;
        std::string uplink_endpoint;
        uint32_t weight;
        uint64_t last_heartbeat_ms;

        registered_service_t () :
            service_type (0),
            weight (1),
            last_heartbeat_ms (0)
        {
        }
    };

    ctx_t *_ctx;
    uint32_t _tag;
    service_runtime_base_t _lifecycle;

    atomic_counter_t _stop;
    uint64_t _task_id;
    void *_sub_socket;
    std::set<std::string> _connected_endpoints;

    mutex_t _sync;
    mutex_t _uplink_sync;
    std::set<std::string> _registry_bootstrap_endpoints;
    std::set<std::string> _bootstrapped_registry_endpoints;
    std::map<std::string, bootstrap_state_t> _bootstrap_states;
    std::set<std::string> _registry_pub_endpoints;
    std::set<std::string> _registry_uplink_endpoints;
    std::string _latest_registry_uplink_endpoint;
    std::map<std::string, socket_base_t *> _report_dealers;
    std::map<std::string, socket_base_t *> _control_dealers;
    std::map<std::string, service_state_t> _services;
    std::map<uint32_t, uint64_t> _registry_seq;
    std::set<discovery_observer_t *> _observers;
    condition_variable_t _observer_cv;
    size_t _observer_callbacks_inflight;
    uint64_t _update_seq;
    std::map<std::string, uint64_t> _service_seq;
    struct socket_opt_t
    {
        int option;
        std::vector<unsigned char> value;
    };
    std::vector<socket_opt_t> _sub_opts;
    uint16_t _service_type;
    bool _discovery_summary_enabled;
    zlink_routing_id_t _routing_id;
    std::string _routing_id_override;
    bool _routing_id_locked;
    uint32_t _heartbeat_interval_ms;
    std::map<registered_service_key_t, registered_service_t> _registered_services;
    std::map<topology_key_t, topology_summary_t> _summary_store;
    std::map<gateway_peer_key_t, gateway_peer_summary_t> _gateway_peer_summary_store;
    service_monitor_hub_t _monitor;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (discovery_t)
};
}

#endif
