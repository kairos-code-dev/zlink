/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_DISCOVERY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_DISCOVERY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "services/common/service_monitor.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;

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
};

class discovery_t
{
  public:
    discovery_t (ctx_t *ctx_, uint16_t service_type_);
    ~discovery_t ();

    bool check_tag () const;

    int connect_registry (const char *registry_pub_endpoint_);
    int connect_registry_router (const char *registry_router_endpoint_);
    int subscribe (const char *service_name_);
    int unsubscribe (const char *service_name_);
    int set_routing_id (const void *data_, size_t size_);
    int routing_id (zlink_routing_id_t *out_) const;
    int set_socket_option (int socket_role_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_);
    void *monitor_open (int events_);

    int get_receivers (const char *service_name_,
                       zlink_receiver_info_t *providers_,
                       size_t *count_);
    int receiver_count (const char *service_name_);
    int service_available (const char *service_name_);
    int destroy ();

    uint16_t service_type () const { return _service_type; }

    void snapshot_providers (const std::string &service_name_,
                             std::vector<provider_info_t> *out_);
    void snapshot_registry_router_endpoints (std::vector<std::string> *out_);
    uint64_t update_seq ();
    uint64_t service_update_seq (const std::string &service_name_);
    void add_observer (discovery_observer_t *observer_);
    void remove_observer (discovery_observer_t *observer_);

  private:
    struct service_state_t
    {
        std::vector<provider_info_t> providers;
    };

    static void control_task (void *arg_);
    void tick ();
    int ensure_sub_socket ();
    void close_sub_socket ();
    bool ensure_routing_id ();
    void handle_service_list (const std::vector<zlink_msg_t> &frames_);
    void notify_observers (const std::set<std::string> &services_);
    int ensure_topology_reporter ();
    void report_topology (const std::string &service_name_,
                          uint16_t state_,
                          uint32_t ready_count_,
                          int32_t error_code_);

    ctx_t *_ctx;
    uint32_t _tag;

    atomic_counter_t _stop;
    uint64_t _task_id;
    void *_sub_socket;
    socket_base_t *_report_dealer;
    std::set<std::string> _connected_endpoints;
    std::set<std::string> _connected_router_endpoints;

    mutex_t _sync;
    std::set<std::string> _registry_endpoints;
    std::set<std::string> _registry_router_endpoints;
    std::map<std::string, service_state_t> _services;
    std::map<uint32_t, uint64_t> _registry_seq;
    std::set<std::string> _subscriptions;
    std::set<discovery_observer_t *> _observers;
    uint64_t _update_seq;
    std::map<std::string, uint64_t> _service_seq;
    struct socket_opt_t
    {
        int option;
        std::vector<unsigned char> value;
    };
    std::vector<socket_opt_t> _sub_opts;
    uint16_t _service_type;
    zlink_routing_id_t _routing_id;
    std::string _routing_id_override;
    service_monitor_hub_t _monitor;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (discovery_t)
};
}

#endif
