/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_GATEWAY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_GATEWAY_HPP_INCLUDED__

#include <zlink.h>

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_monitor.hpp"
#include "services/discovery/discovery.hpp"
#include "utils/clock.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

namespace zlink
{
class clock_t;
class socket_base_t;
struct gateway_runtime_t;
struct gateway_access_t;
struct gateway_service_pool_t
{
    struct send_snapshot_t
    {
        std::vector<zlink_routing_id_t> routing_ids;
        std::vector<uint32_t> weights;
        std::vector<std::string> endpoints;
        socket_base_t *router_socket;

        send_snapshot_t () : router_socket (NULL) {}
    };

    struct control_route_t
    {
        zlink_routing_id_t routing_id;
        uint32_t weight;

        control_route_t () : weight (0) { routing_id.size = 0; }
    };

    struct control_snapshot_t
    {
        std::map<std::string, control_route_t> routes_by_endpoint;
        uint64_t last_seen_seq;

        control_snapshot_t () : last_seen_seq (0) {}
    };

    std::string service_name;
    std::shared_ptr<const send_snapshot_t> send_snapshot;
    std::shared_ptr<control_snapshot_t> control_snapshot;
    size_t rr_cursor;
    int lb_strategy;
    bool dirty;

    gateway_service_pool_t ();
};

struct gateway_route_delta_t
{
    uint32_t event_type;
    std::string service_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
    uint32_t ready_count;
    int32_t error_code;

    gateway_route_delta_t () : event_type (0), ready_count (0), error_code (0)
    {
        routing_id.size = 0;
    }
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
    service_public_api_guard_t &public_api_guard () { return _public_api; }

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
    int update_peer_weight (const zlink_routing_id_t *routing_id_,
                            uint32_t weight_);
    int set_socket_option (int option_,
                           const void *optval_,
                           size_t optvallen_);
    int get_socket_option (int option_, void *optval_, size_t *optvallen_);
    int snapshot_status (zlink_gateway_status_t *out_) const;
    int set_tls_server (const char *cert_, const char *key_);
    void *monitor_open (int events_);
    void lock_routing_id ();
    int fill_monitor_snapshot (zlink_monitor_snapshot_t *out_);
    int set_handler (zlink_socket_msg_handler_fn handler_, void *userdata_);
    int set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                void *userdata_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    void on_service_update (const std::string &service_name_);
    void on_discovery_destroyed (discovery_t *discovery_) ZLINK_OVERRIDE;

    int destroy ();

  private:
    friend struct gateway_access_t;

    gateway_service_pool_t *get_or_create_pool (const std::string &service_name_);
    gateway_service_pool_t *get_or_create_pool_cached ();
    int init_router_socket ();
    int ensure_router_socket ();
    std::string resolve_advertise (const char *advertise_endpoint_) const;
    void refresh_pool (gateway_service_pool_t *pool_,
                       const std::vector<provider_info_t> &providers_,
                       uint64_t seq_);
    bool select_provider (
      gateway_service_pool_t *pool_,
      const gateway_service_pool_t::send_snapshot_t *snapshot_,
      size_t *index_out_);
    bool find_provider_index (gateway_service_pool_t *pool_,
                              const zlink_routing_id_t *rid_,
                              size_t *index_out_);
    int ensure_refresh_task_running ();
    bool can_suspend_refresh_task () const;
    int send_request_frames (gateway_service_pool_t *pool_,
                             size_t provider_index_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             int flags_);
    void *router ();
    int ensure_facade_mode () const;
    bool enter_pollable_mode ();
    void dispatch_message (const zlink_routing_id_t *source_rid_,
                           zlink_msg_t *parts_,
                           size_t part_count_);
    void dispatch_send_ready ();

    void process_monitor_events ();
    void emit_route_deltas (const std::vector<gateway_route_delta_t> &deltas_);
    void emit_events (const zlink_service_event_t *events_, size_t count_);
    void fill_event (zlink_service_event_t *out_,
                     uint32_t event_type_,
                     const std::string &service_name_,
                     const std::string &endpoint_,
                     const zlink_routing_id_t *routing_id_,
                     uint32_t value_,
                     int32_t error_code_) const;
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
    void report_gateway_peer (const std::string &service_name_,
                              const std::string &peer_endpoint_,
                              const zlink_routing_id_t &peer_routing_id_,
                              uint32_t weight_,
                              uint16_t state_,
                              uint64_t connected_since_ms);
    void sync_gateway_peer_reports (uint64_t now_ms_);
    static void refresh_task (void *arg_);
    void refresh_tick ();

    ctx_t *_ctx;
    discovery_t *_discovery;
    uint32_t _tag;
    gateway_runtime_t *_runtime;
    bool _use_lock;
    bool _pollable_mode;
    bool _routing_id_locked;
    bool _service_ready_emitted;
    uint32_t _refresh_interval_ms;
    mutex_t _sync;
    mutex_t _send_sync;

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
    int _last_summary_error;
    uint64_t _summary_last_changed_ms;
    std::string _tls_server_cert;
    std::string _tls_server_key;
    std::atomic<zlink_socket_msg_handler_fn> _handler;
    std::atomic<void *> _handler_userdata;
    std::atomic<zlink_send_ready_handler_fn> _send_ready_handler;
    std::atomic<void *> _send_ready_handler_userdata;
    service_public_api_guard_t _public_api;
    service_monitor_hub_t _monitor;

    friend struct gateway_runtime_t;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (gateway_t)
};
}

#endif
