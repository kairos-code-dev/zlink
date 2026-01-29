/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_DISCOVERY_GATEWAY_HPP_INCLUDED__
#define __ZMQ_DISCOVERY_GATEWAY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "core/thread.hpp"
#include "sockets/thread_safe_socket.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "utils/mutex.hpp"

#include "discovery/discovery.hpp"

#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zmq
{
class gateway_t : public discovery_listener_t
{
  public:
    gateway_t (ctx_t *ctx_, discovery_t *discovery_);
    ~gateway_t ();

    bool check_tag () const;
    void on_discovery_event (int event_,
                             const std::string &service_name_);

    int send (const char *service_name_,
              zmq_msg_t *parts_,
              size_t part_count_,
              int flags_,
              uint64_t *request_id_out_);

    int recv (zmq_msg_t **parts_,
              size_t *part_count_,
              int flags_,
              char *service_name_out_,
              uint64_t *request_id_out_);

    uint64_t request (const char *service_name_,
                      zmq_msg_t *parts_,
                      size_t part_count_,
                      zmq_gateway_request_cb_fn callback_,
                      int timeout_ms_);

    uint64_t request_send (const char *service_name_,
                           zmq_msg_t *parts_,
                           size_t part_count_,
                           int flags_);

    int request_recv (zmq_gateway_completion_t *completion_,
                      int timeout_ms_);

    int set_lb_strategy (const char *service_name_, int strategy_);
    int connection_count (const char *service_name_);

    void *threadsafe_router (const char *service_name_);

    int destroy ();

  private:
    struct service_pool_t
    {
        std::string service_name;
        socket_base_t *socket;
        thread_safe_socket_t *threadsafe;
        std::vector<provider_info_t> providers;
        std::map<std::string, zmq_routing_id_t> routing_map;
        std::vector<std::string> endpoints;
        size_t rr_index;
        int lb_strategy;
    };

    struct pending_request_t
    {
        std::string service_name;
        zmq_gateway_request_cb_fn callback;
        bool has_deadline;
        std::chrono::steady_clock::time_point deadline;
    };

    struct completion_entry_t
    {
        std::string service_name;
        uint64_t request_id;
        int error;
        std::vector<msg_t> parts;
    };

    struct callback_entry_t
    {
        zmq_gateway_request_cb_fn callback;
        uint64_t request_id;
        int error;
        std::vector<msg_t> parts;
    };

    service_pool_t *get_or_create_pool (const std::string &service_name_);
    void refresh_pool (service_pool_t *pool_);
    bool select_provider (service_pool_t *pool_, zmq_routing_id_t *rid_);

    static void run (void *arg_);
    void loop ();

    int send_request_frames (service_pool_t *pool_,
                             const zmq_routing_id_t &rid_,
                             uint64_t request_id_,
                             zmq_msg_t *parts_,
                             size_t part_count_,
                             int flags_);
    int recv_from_pool (service_pool_t *pool_,
                        uint64_t *request_id_,
                        std::vector<msg_t> *parts_);
    void handle_response (uint64_t request_id_, std::vector<msg_t> *parts_,
                          const std::string &fallback_service_,
                          std::deque<callback_entry_t> *callbacks_);

    void close_parts (std::vector<msg_t> *parts_);
    zmq_msg_t *alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                      size_t *count_);
    int wait_for_completion (completion_entry_t *entry_,
                             int timeout_ms_);

    ctx_t *_ctx;
    discovery_t *_discovery;
    uint32_t _tag;

    mutex_t _sync;
    std::map<std::string, service_pool_t> _pools;
    std::map<uint64_t, pending_request_t> _pending;
    uint64_t _next_request_id;
    std::set<std::string> _refresh_queue;

    atomic_counter_t _stop;
    thread_t _worker;

    mutex_t _completion_sync;
    condition_variable_t _completion_cv;
    std::deque<completion_entry_t> _completion_queue;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (gateway_t)
};
}

#endif
