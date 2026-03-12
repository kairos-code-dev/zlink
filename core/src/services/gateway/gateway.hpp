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
struct gateway_runtime_t;
struct gateway_service_pool_t
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

struct gateway_manual_route_t
{
    zlink_routing_id_t routing_id;
    uint32_t weight;
};

class gateway_t : public discovery_observer_t
{
  public:
    gateway_t (ctx_t *ctx_, const char *service_name_,
               const char *routing_id_ = NULL);
    ~gateway_t ();

    bool check_tag () const;

    int send (zlink_msg_t *parts_, size_t part_count_, int flags_);
    int attach_discovery (discovery_t *discovery_);
    int bind (const char *endpoint_);
    int connect (const char *endpoint_, const zlink_routing_id_t *routing_id_);
    int disconnect (const char *endpoint_);
    int register_service (const char *advertise_endpoint_, uint32_t weight_);
    int update_weight (uint32_t weight_);
    int unregister_service ();
    int send_rid (const zlink_routing_id_t *routing_id_,
                  zlink_msg_t *parts_,
                  size_t part_count_,
                  int flags_);

    int set_lb_strategy (int strategy_);
    int set_routing_id (const void *data_, size_t size_);
    int routing_id (zlink_routing_id_t *out_);
    int last_endpoint (char *endpoint_out_, size_t *size_out_) const;
    int peer_info (const zlink_routing_id_t *routing_id_,
                   zlink_gateway_peer_info_t *info_out_) const;
    int router_peers (zlink_gateway_peer_info_t *peers_, size_t *count_) const;
    int update_peer_weight (const zlink_routing_id_t *routing_id_,
                            uint32_t weight_);
    int set_option (int option_, const void *optval_, size_t optvallen_);
    int set_socket_option (int option_,
                           const void *optval_,
                           size_t optvallen_);
    int set_tls_server (const char *cert_, const char *key_);
    void *monitor_open (int events_);
    void *router ();
    bool enter_pollable_mode ();
    void lock_routing_id ();
    int ensure_facade_mode () const;
    int connection_count ();
    int set_handler (zlink_socket_msg_handler_fn handler_);
    int set_send_ready_handler (zlink_send_ready_handler_fn handler_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    void on_service_update (const std::string &service_name_);
    void on_discovery_destroyed (discovery_t *discovery_) ZLINK_OVERRIDE;
    void dispatch_message (const zlink_routing_id_t *source_rid_,
                           zlink_msg_t *parts_,
                           size_t part_count_);
    void dispatch_send_ready ();

    int destroy ();

  private:
    gateway_service_pool_t *get_or_create_pool (const std::string &service_name_);
    gateway_service_pool_t *get_or_create_pool_cached ();
    int init_router_socket ();
    int ensure_router_socket ();
    std::string resolve_advertise (const char *advertise_endpoint_) const;
    void refresh_pool (gateway_service_pool_t *pool_,
                       const std::vector<provider_info_t> &providers_,
                       uint64_t seq_);
    bool select_provider (gateway_service_pool_t *pool_, size_t *index_out_);
    bool find_provider_index (gateway_service_pool_t *pool_,
                              const zlink_routing_id_t *rid_,
                              size_t *index_out_);
    int send_request_frames (gateway_service_pool_t *pool_,
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
    gateway_runtime_t *_runtime;
    bool _use_lock;
    bool _pollable_mode;
    bool _routing_id_locked;
    uint32_t _refresh_interval_ms;
    mutex_t _sync;

    std::string _tls_ca;
    std::string _tls_hostname;
    int _tls_trust_system;
    std::string _service_name;
    zlink_routing_id_t _routing_id;
    std::string _routing_id_override;
    std::string _bind_endpoint;
    std::string _server_service_name;
    std::string _advertise_endpoint;
    uint32_t _server_weight;
    std::string _last_register_error;
    std::string _tls_server_cert;
    std::string _tls_server_key;
    std::atomic<zlink_socket_msg_handler_fn> _handler;
    std::atomic<zlink_send_ready_handler_fn> _send_ready_handler;
    service_monitor_hub_t _monitor;

    friend struct gateway_runtime_t;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (gateway_t)
};
}

#endif
