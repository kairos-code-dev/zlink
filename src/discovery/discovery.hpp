/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_DISCOVERY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_DISCOVERY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/thread.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
struct provider_info_t
{
    std::string service_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
    uint32_t weight;
    uint64_t registered_at;
};

class discovery_t
{
  public:
    explicit discovery_t (ctx_t *ctx_);
    ~discovery_t ();

    bool check_tag () const;

    int connect_registry (const char *registry_pub_endpoint_);
    int subscribe (const char *service_name_);
    int unsubscribe (const char *service_name_);

    int get_providers (const char *service_name_,
                       zlink_provider_info_t *providers_,
                       size_t *count_);
    int provider_count (const char *service_name_);
    int service_available (const char *service_name_);
    int destroy ();

    void snapshot_providers (const std::string &service_name_,
                             std::vector<provider_info_t> *out_);

  private:
    struct service_state_t
    {
        std::vector<provider_info_t> providers;
    };

    static void run (void *arg_);
    void loop ();
    void handle_service_list (const std::vector<zlink_msg_t> &frames_);

    ctx_t *_ctx;
    uint32_t _tag;

    atomic_counter_t _stop;
    thread_t _worker;

    mutex_t _sync;
    std::set<std::string> _registry_endpoints;
    std::map<std::string, service_state_t> _services;
    std::map<uint32_t, uint64_t> _registry_seq;
    std::set<std::string> _subscriptions;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (discovery_t)
};
}

#endif
