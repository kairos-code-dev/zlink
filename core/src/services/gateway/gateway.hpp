/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_GATEWAY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_GATEWAY_HPP_INCLUDED__

#include <zlink.h>

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "services/common/service_monitor.hpp"
#include "services/discovery/discovery.hpp"
#include "utils/clock.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <atomic>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

namespace zlink
{
class clock_t;
class socket_base_t;
class gateway_t : public discovery_observer_t
{
  public:
    gateway_t (ctx_t *ctx_, discovery_t *discovery_,
               const char *routing_id_ = NULL);
    ~gateway_t ();

    bool check_tag () const;

    int send (const char *service_name_,
              zlink_msg_t *parts_,
              size_t part_count_,
              int flags_);
    int bind (const char *endpoint_);
    int register_service (const char *service_name_,
                          const char *advertise_endpoint_,
                          uint32_t weight_);
    int update_weight (const char *service_name_, uint32_t weight_);
    int unregister_service (const char *service_name_);
    int recv (zlink_msg_t **parts_,
              size_t *part_count_,
              int flags_,
              char *service_name_out_);
    int send_rid (const char *service_name_,
                  const zlink_routing_id_t *routing_id_,
                  zlink_msg_t *parts_,
                  size_t part_count_,
                  int flags_);

    int set_lb_strategy (const char *service_name_, int strategy_);
    int set_routing_id (const void *data_, size_t size_);
    int routing_id (zlink_routing_id_t *out_);
    int last_endpoint (char *endpoint_out_, size_t *size_out_) const;
    int peer_info (const zlink_routing_id_t *routing_id_,
                   zlink_peer_info_t *info_out_) const;
    int set_option (int option_, const void *optval_, size_t optvallen_);
    int set_socket_option (int option_,
                           const void *optval_,
                           size_t optvallen_);
    int set_tls_server (const char *cert_, const char *key_);
    void *monitor_open (int events_);
    void *poller_socket ();
    void *router ();
    bool enter_pollable_mode ();
    void lock_routing_id ();
    int ensure_facade_mode () const;
    int connection_count (const char *service_name_);
    int set_handler (zlink_gateway_handler_fn handler_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    void on_service_update (const std::string &service_name_);
    void dispatch_message (const zlink_routing_id_t *source_rid_,
                           zlink_msg_t *parts_,
                           size_t part_count_);

    int destroy ();

  private:
    struct service_pool_t
    {
        std::string service_name;
        std::vector<zlink_routing_id_t> routing_ids;
        std::vector<uint32_t> weights;
        std::vector<std::string> endpoints;
        size_t rr_index;
        int lb_strategy;
        uint64_t last_seen_seq;
        bool dirty;
    };

    service_pool_t *get_or_create_pool (const std::string &service_name_);
    service_pool_t *get_or_create_pool_cached (const char *service_name_);
    int init_router_socket ();
    int ensure_router_socket ();
    std::string resolve_advertise (const char *advertise_endpoint_) const;
    bool classify_message (const zlink_routing_id_t *source_rid_,
                           zlink_gateway_msg_kind_t *kind_out_,
                           std::string *service_name_out_) const;
    void refresh_pool (service_pool_t *pool_,
                       const std::vector<provider_info_t> &providers_,
                       uint64_t seq_);
    bool select_provider (service_pool_t *pool_, size_t *index_out_);
    bool find_provider_index (service_pool_t *pool_,
                              const zlink_routing_id_t *rid_,
                              size_t *index_out_);
    int send_request_frames (service_pool_t *pool_,
                             size_t provider_index_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             int flags_);

    void process_monitor_events ();
    void emit_event (uint32_t event_type_,
                     const std::string &service_name_,
                     const std::string &endpoint_,
                     const zlink_routing_id_t *routing_id_,
                     uint32_t value_,
                     int32_t error_code_);
    void emit_control_callback (uint32_t event_type_,
                                const std::string &service_name_,
                                int32_t error_code_);
    void report_topology (const std::string &service_name_,
                          const std::string &endpoint_,
                          uint16_t state_,
                          uint32_t ready_count_,
                          int32_t error_code_);
    static void refresh_task (void *arg_);
    void refresh_tick ();

    ctx_t *_ctx;
    discovery_t *_discovery;
    uint32_t _tag;

    std::map<std::string, service_pool_t> _pools;
    std::string _last_service_name;
    service_pool_t *_last_pool;
    std::map<std::string, std::string> _endpoint_to_service;
    std::map<std::string, std::string> _routing_id_to_service;
    std::set<std::string> _ready_endpoints;
    std::set<std::string> _inflight_endpoints;
    std::map<std::string, std::string> _inflight_rid_by_endpoint;
    std::map<std::string, uint64_t> _rid_connect_not_before_ms;
    std::set<std::string> _down_endpoints;
    std::map<std::string, uint64_t> _down_until_ms;
    bool _force_refresh_all;
    std::set<std::string> _pending_updates;
    void *_monitor_socket;
    socket_base_t *_router_socket;
    bool _use_lock;
    bool _pollable_mode;
    bool _routing_id_locked;
    atomic_counter_t _stop;
    uint64_t _refresh_task_id;
    uint32_t _refresh_interval_ms;
    mutex_t _sync;
    clock_t _clock;

    std::string _tls_ca;
    std::string _tls_hostname;
    int _tls_trust_system;
    zlink_routing_id_t _routing_id;
    std::string _routing_id_override;
    std::string _bind_endpoint;
    std::string _server_service_name;
    std::string _advertise_endpoint;
    uint32_t _server_weight;
    std::string _last_register_error;
    std::string _tls_server_cert;
    std::string _tls_server_key;
    std::atomic<zlink_gateway_handler_fn> _handler;
    service_monitor_hub_t _monitor;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (gateway_t)
};
}

#endif
