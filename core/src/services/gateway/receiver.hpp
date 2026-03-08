/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_RECEIVER_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_RECEIVER_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "services/common/service_monitor.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <string>
#include <vector>

namespace zlink
{
enum receiver_socket_role_t
{
    receiver_socket_router = 1,
    receiver_socket_dealer = 2
};

class receiver_t
{
  public:
    explicit receiver_t (ctx_t *ctx_, const char *routing_id_ = NULL);
    ~receiver_t ();

    bool check_tag () const;

    int bind (const char *endpoint_);
    int connect_registry (const char *registry_router_endpoint_);
    int register_service (const char *service_name_,
                          const char *advertise_endpoint_,
                          uint32_t weight_);
    int update_weight (const char *service_name_, uint32_t weight_);
    int unregister_service (const char *service_name_);
    int register_result (const char *service_name_,
                         int *status_,
                         char *resolved_endpoint_,
                         char *error_message_);
    int set_tls_server (const char *cert_, const char *key_);
    int recv (zlink_msg_t **parts_,
              size_t *part_count_,
              int flags_,
              zlink_routing_id_t *routing_id_out_);
    int last_endpoint (char *endpoint_out_, size_t *size_out_) const;
    int peer_info (const zlink_routing_id_t *routing_id_,
                   zlink_peer_info_t *info_out_) const;
    int set_routing_id (const void *data_, size_t size_);
    int routing_id (zlink_routing_id_t *out_) const;
    int set_option (int option_, const void *optval_, size_t optvallen_);
    void *monitor_open (int events_);
    int set_socket_option (int socket_role_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_);
    void *poller_socket ();
    void *router ();
    void lock_routing_id ();
    int destroy ();

  private:
    static void heartbeat_task (void *arg_);
    void heartbeat_tick ();
    bool ensure_routing_id ();
    std::string resolve_advertise (const char *advertise_endpoint_);

    ctx_t *_ctx;
    uint32_t _tag;

    socket_base_t *_router;
    socket_base_t *_dealer;

    zlink_routing_id_t _routing_id;

    std::string _bind_endpoint;
    std::string _registry_endpoint;

    std::string _service_name;
    std::string _advertise_endpoint;
    uint32_t _weight;
    std::string _routing_id_override;
    bool _routing_id_locked;

    int _last_status;
    std::string _last_resolved;
    std::string _last_error;

    uint32_t _heartbeat_interval_ms;
    uint64_t _next_heartbeat_ms;
    atomic_counter_t _stop;
    uint64_t _heartbeat_task_id;
    socket_base_t *_heartbeat_dealer;
    std::string _heartbeat_registry_endpoint;

    mutex_t _sync;

    std::string _tls_cert;
    std::string _tls_key;
    service_monitor_hub_t _monitor;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (receiver_t)
};
}

#endif
