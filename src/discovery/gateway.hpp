/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_GATEWAY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_GATEWAY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "discovery/discovery.hpp"

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace zlink
{
class gateway_t
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

    int set_lb_strategy (const char *service_name_, int strategy_);
    int connection_count (const char *service_name_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);

    int destroy ();

    socket_base_t *get_router_socket (const char *service_name_);

  private:
    struct service_pool_t
    {
        std::string service_name;
        socket_base_t *socket;
        std::vector<provider_info_t> providers;
        std::vector<std::string> endpoints;
        size_t rr_index;
        int lb_strategy;
    };

    struct completion_entry_t
    {
        std::string service_name;
        int error;
        std::vector<msg_t> parts;
    };

    service_pool_t *get_or_create_pool (const std::string &service_name_);
    void refresh_pool (service_pool_t *pool_);
    bool select_provider (service_pool_t *pool_, zlink_routing_id_t *rid_);

    int send_request_frames (service_pool_t *pool_,
                             const zlink_routing_id_t &rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             int flags_);
    int recv_from_pool (service_pool_t *pool_,
                        std::vector<msg_t> *parts_);
    bool recv_any (completion_entry_t *entry_, int timeout_ms_);

    void close_parts (std::vector<msg_t> *parts_);
    zlink_msg_t *alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                      size_t *count_);

    ctx_t *_ctx;
    discovery_t *_discovery;
    uint32_t _tag;

    std::map<std::string, service_pool_t> _pools;

    std::string _tls_ca;
    std::string _tls_hostname;
    int _tls_trust_system;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (gateway_t)
};
}

#endif
