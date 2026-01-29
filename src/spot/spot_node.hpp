/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "core/thread.hpp"
#include "discovery/discovery.hpp"
#include "sockets/thread_safe_socket.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "utils/mutex.hpp"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class spot_t;

class spot_node_t
{
  public:
    explicit spot_node_t (ctx_t *ctx_);
    ~spot_node_t ();

    bool check_tag () const;

    int bind (const char *endpoint_);
    int connect_registry (const char *registry_router_endpoint_);
    int connect_peer_pub (const char *peer_pub_endpoint_);
    int disconnect_peer_pub (const char *peer_pub_endpoint_);
    int register_node (const char *service_name_,
                       const char *advertise_endpoint_);
    int unregister_node (const char *service_name_);
    int set_discovery (discovery_t *discovery_,
                       const char *service_name_);

    spot_t *create_spot (bool threadsafe_);
    void remove_spot (spot_t *spot_);

    int publish (spot_t *spot_,
                 const char *topic_,
                 zlink_msg_t *parts_,
                 size_t part_count_,
                 int flags_);
    int subscribe (spot_t *spot_, const char *topic_);
    int subscribe_pattern (spot_t *spot_, const char *pattern_);
    int unsubscribe (spot_t *spot_, const char *topic_or_pattern_);
    int topic_create (const char *topic_, int mode_);
    int topic_destroy (const char *topic_);

    int destroy ();

  private:
    friend class spot_t;
    struct ringbuffer_t
    {
        ringbuffer_t () : start_seq (1), hwm (1024) {}
        uint64_t start_seq;
        size_t hwm;
        std::deque<std::vector<msg_t> > entries;
    };

    struct topic_state_t
    {
        int mode;
        ringbuffer_t ring;
    };

    static void run (void *arg_);
    void loop ();
    void process_sub ();
    void dispatch_local (const std::string &topic_,
                         const std::vector<msg_t> &payload_);
    void refresh_peers ();
    void send_heartbeat (uint64_t now_ms_);

    static bool validate_topic (const char *topic_, std::string *out_);
    static bool validate_pattern (const char *pattern_, std::string *prefix_);
    static bool validate_service_name (const std::string &name_);
    static std::string resolve_advertise (const std::string &bind_endpoint_);

    bool topic_is_ringbuffer (const std::string &topic_) const;
    void ensure_ringbuffer_topic (const std::string &topic_);

    void add_filter (const std::string &filter_);
    void remove_filter (const std::string &filter_);

    ctx_t *_ctx;
    uint32_t _tag;

    socket_base_t *_pub;
    thread_safe_socket_t *_pub_threadsafe;
    socket_base_t *_sub;
    thread_safe_socket_t *_sub_threadsafe;
    socket_base_t *_dealer;
    thread_safe_socket_t *_dealer_threadsafe;

    std::vector<std::string> _bind_endpoints;
    std::set<std::string> _peer_endpoints;
    std::set<std::string> _registry_endpoints;

    uint32_t _node_id;
    zlink_routing_id_t _routing_id;

    bool _registered;
    std::string _service_name;
    std::string _advertise_endpoint;
    uint32_t _heartbeat_interval_ms;
    uint64_t _last_heartbeat_ms;

    discovery_t *_discovery;
    std::string _discovery_service;
    uint64_t _next_discovery_refresh_ms;

    mutex_t _sync;
    std::set<spot_t *> _spots;
    std::map<std::string, size_t> _filter_refcount;
    std::map<std::string, topic_state_t> _topics;

    atomic_counter_t _stop;
    thread_t _worker;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_node_t)
};
}

#endif
