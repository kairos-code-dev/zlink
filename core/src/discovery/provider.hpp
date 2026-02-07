/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_PROVIDER_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_PROVIDER_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/thread.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <string>
#include <vector>

namespace zlink
{
class provider_t
{
  public:
    explicit provider_t (ctx_t *ctx_, const char *routing_id_ = NULL);
    ~provider_t ();

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
    int set_socket_option (int socket_role_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_);
    void *router ();
    int destroy ();

  private:
    static void heartbeat_worker (void *arg_);
    void send_heartbeat ();
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

    int _last_status;
    std::string _last_resolved;
    std::string _last_error;

    uint32_t _heartbeat_interval_ms;
    atomic_counter_t _stop;
    thread_t _heartbeat_thread;

    mutex_t _sync;

    std::string _tls_cert;
    std::string _tls_key;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (provider_t)
};
}

#endif
