/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_GATEWAY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_GATEWAY_HPP_INCLUDED__

#include <zlink.h>

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "core/thread.hpp"
#include "discovery/discovery.hpp"
#include "utils/clock.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

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
    gateway_t (ctx_t *ctx_, discovery_t *discovery_);
    ~gateway_t ();

    bool check_tag () const;

    int send (const char *service_name_,
              zlink_msg_t *parts_,
              size_t part_count_,
              int flags_);
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
    int set_router_option (int option_,
                           const void *optval_,
                           size_t optvallen_);
    int connection_count (const char *service_name_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    void on_service_update (const std::string &service_name_);

    int destroy ();

  private:
    struct service_pool_t
    {
        std::string service_name;
        std::vector<zlink_routing_id_t> routing_ids;
        std::vector<std::string> endpoints;
        size_t rr_index;
        int lb_strategy;
        uint64_t last_seen_seq;
        bool dirty;
    };

    service_pool_t *get_or_create_pool (const std::string &service_name_);
    service_pool_t *get_or_create_pool_cached (const char *service_name_);
    int ensure_router_socket ();
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
    static void refresh_run (void *arg_);
    void refresh_loop ();

    ctx_t *_ctx;
    discovery_t *_discovery;
    uint32_t _tag;

    std::map<std::string, service_pool_t> _pools;
    std::string _last_service_name;
    service_pool_t *_last_pool;
    std::map<std::string, std::string> _endpoint_to_service;
    std::set<std::string> _ready_endpoints;
    std::set<std::string> _down_endpoints;
    std::map<std::string, uint64_t> _down_until_ms;
    bool _force_refresh_all;
    std::set<std::string> _pending_updates;
    void *_monitor_socket;
    socket_base_t *_router_socket;
    struct router_opt_t
    {
        int option;
        std::vector<unsigned char> value;
    };
    std::vector<router_opt_t> _router_opts;
    bool _use_lock;
    atomic_counter_t _stop;
    thread_t _refresh_worker;
    mutex_t _sync;
    clock_t _clock;

    std::string _tls_ca;
    std::string _tls_hostname;
    int _tls_trust_system;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (gateway_t)
};
}

#endif
