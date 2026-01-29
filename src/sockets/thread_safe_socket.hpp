/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_THREAD_SAFE_SOCKET_HPP_INCLUDED__
#define __ZMQ_THREAD_SAFE_SOCKET_HPP_INCLUDED__

#include <boost/asio.hpp>
#include <errno.h>
#include <stddef.h>
#include <chrono>
#include <deque>
#include <unordered_map>
#include <vector>

#include "core/msg.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "utils/macros.hpp"
#include "utils/mutex.hpp"
#include "zmq.h"

namespace zmq
{
class ctx_t;
class msg_t;
class socket_base_t;

static const uint32_t threadsafe_socket_tag_value = 0x7473736b;

class thread_safe_socket_t
{
  public:
    thread_safe_socket_t (ctx_t *ctx_, socket_base_t *socket_);
    ~thread_safe_socket_t ();

    bool check_tag () const;

    socket_base_t *get_socket () const;
    ctx_t *get_ctx () const;

    int close ();
    void wait_for_idle ();

    int setsockopt (int option_, const void *optval_, size_t optvallen_);
    int getsockopt (int option_, void *optval_, size_t *optvallen_);

    int bind (const char *endpoint_);
    int connect (const char *endpoint_);
    int term_endpoint (const char *endpoint_);

    int send (msg_t *msg_, int flags_);
    int recv (msg_t *msg_, int flags_);

    bool has_in ();
    bool has_out ();

    int join (const char *group_);
    int leave (const char *group_);

    int monitor (const char *endpoint_,
                 uint64_t events_,
                 int event_version_,
                 int type_);

    int socket_stats (zmq_socket_stats_t *stats_);
    int socket_stats_ex (zmq_socket_stats_ex_t *stats_);
    int socket_peer_info (const zmq_routing_id_t *routing_id_,
                          zmq_peer_info_t *info_);
    int socket_peer_routing_id (int index_, zmq_routing_id_t *out_);
    int socket_peer_count ();
    int socket_peers (zmq_peer_info_t *peers_, size_t *count_);
    int get_peer_state (const void *routing_id_, size_t routing_id_size_);

    uint64_t request (const zmq_routing_id_t *routing_id_,
                      zmq_msg_t *parts_,
                      size_t part_count_,
                      zmq_request_cb_fn callback_,
                      int timeout_ms_);
    uint64_t group_request (const zmq_routing_id_t *routing_id_,
                            uint64_t group_id_,
                            zmq_msg_t *parts_,
                            size_t part_count_,
                            zmq_request_cb_fn callback_,
                            int timeout_ms_);
    uint64_t request_send (const zmq_routing_id_t *routing_id_,
                           zmq_msg_t *parts_,
                           size_t part_count_);
    int request_recv (zmq_completion_t *completion_, int timeout_ms_);
    int pending_requests ();
    int cancel_all_requests ();

    int on_request (zmq_server_cb_fn handler_);
    int reply (const zmq_routing_id_t *routing_id_,
               uint64_t request_id_,
               zmq_msg_t *parts_,
               size_t part_count_);
    int reply_simple (zmq_msg_t *parts_, size_t part_count_);

  private:
    struct pending_request_t
    {
        uint64_t request_id;
        uint64_t group_id;
        zmq_routing_id_t routing_id;
        bool has_routing_id;
        bool use_callback;
        zmq_request_cb_fn callback;
        std::vector<msg_t> parts;
        std::chrono::steady_clock::time_point deadline;
        bool has_deadline;
        bool correlate;
    };

    struct group_queue_t
    {
        group_queue_t () : inflight_id (0) {}
        uint64_t inflight_id;
        std::deque<uint64_t> pending;
    };

    struct routing_id_key_t
    {
        uint8_t size;
        uint8_t data[255];
    };

    struct routing_id_key_hash
    {
        size_t operator() (const routing_id_key_t &key) const
        {
            size_t hash = 1469598103934665603ULL;
            for (uint8_t i = 0; i < key.size; ++i) {
                hash ^= key.data[i];
                hash *= 1099511628211ULL;
            }
            return hash;
        }
    };

    struct routing_id_key_equal
    {
        bool operator() (const routing_id_key_t &lhs,
                         const routing_id_key_t &rhs) const
        {
            if (lhs.size != rhs.size)
                return false;
            return lhs.size == 0
                   || memcmp (lhs.data, rhs.data, lhs.size) == 0;
        }
    };

    struct completion_entry_t
    {
        uint64_t request_id;
        std::vector<msg_t> parts;
        int error;
    };

    struct incoming_message_t
    {
        zmq_routing_id_t routing_id;
        bool has_routing_id;
        uint64_t request_id;
        std::vector<msg_t> parts;
    };

    template <typename Result, typename Func>
    Result dispatch (Func func_, int *err_);

    uint64_t request_common (const zmq_routing_id_t *routing_id_,
                             uint64_t group_id_,
                             zmq_msg_t *parts_,
                             size_t part_count_,
                             zmq_request_cb_fn callback_,
                             int timeout_ms_,
                             bool use_callback_);
    int reply_common (const zmq_routing_id_t *routing_id_,
                      uint64_t request_id_,
                      zmq_msg_t *parts_,
                      size_t part_count_);
    void ensure_request_pump ();
    void schedule_request_pump (std::chrono::milliseconds delay_);
    void pump_requests (const boost::system::error_code &ec_);
    bool recv_request_message (incoming_message_t *out_);
    void handle_incoming_message (incoming_message_t *msg_);
    void handle_request_complete (pending_request_t &req_,
                                  std::vector<msg_t> *parts_,
                                  int error_);
    void handle_group_complete (pending_request_t &req_);
    void process_timeouts ();
    int send_request (pending_request_t &req_);
    int send_request_direct (pending_request_t &req_,
                             zmq_msg_t *parts_,
                             size_t part_count_,
                             int socket_type_);
    void enqueue_non_correlate (const pending_request_t &req_);
    void remove_non_correlate (const pending_request_t &req_);
    int cancel_all_requests_internal (int error_);
    int get_socket_type ();
    bool get_request_correlate ();
    int get_request_timeout ();
    bool validate_request_params (int socket_type_,
                                  const zmq_routing_id_t *routing_id_,
                                  zmq_msg_t *parts_,
                                  size_t part_count_,
                                  zmq_request_cb_fn callback_,
                                  bool require_callback_);
    bool copy_parts_in (zmq_msg_t *parts_,
                        size_t part_count_,
                        std::vector<msg_t> *out_);
    void close_msg_array (zmq_msg_t *parts_, size_t part_count_);
    void close_parts (std::vector<msg_t> *parts_);
    zmq_msg_t *alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                      size_t *count_);
    routing_id_key_t make_routing_id_key (const zmq_routing_id_t &rid_) const;
    void add_active ();
    void release_active ();

    uint32_t _tag;
    ctx_t *_ctx;
    socket_base_t *_socket;
    boost::asio::strand<boost::asio::io_context::executor_type> _strand;
    mutex_t _sync;
    condition_variable_t _sync_cv;
    boost::asio::steady_timer _request_timer;
    bool _request_pump_active;
    uint64_t _next_request_id;
    atomic_counter_t _active_calls;
    std::unordered_map<uint64_t, pending_request_t> _pending_requests;
    std::unordered_map<uint64_t, group_queue_t> _group_queues;
    std::deque<uint64_t> _non_correlate_queue;
    std::unordered_map<routing_id_key_t,
                       std::deque<uint64_t>,
                       routing_id_key_hash,
                       routing_id_key_equal>
      _non_correlate_by_rid;
    zmq_server_cb_fn _request_handler;
    bool _in_request_handler;
    zmq_routing_id_t _current_routing_id;
    uint64_t _current_request_id;
    mutex_t _completion_sync;
    condition_variable_t _completion_cv;
    std::deque<completion_entry_t> _completion_queue;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (thread_safe_socket_t)
};

template <typename Result, typename Func>
Result thread_safe_socket_t::dispatch (Func func_, int *err_)
{
    add_active ();

    if (_strand.running_in_this_thread ()) {
        Result res = func_ ();
        if (err_)
            *err_ = errno;
        release_active ();
        return res;
    }

    Result res = Result ();
    int err = 0;
    bool done = false;

    boost::asio::post (_strand, [this, func_, &res, &err, &done]() mutable {
        Result local_res = func_ ();
        int local_err = errno;

        scoped_lock_t lock (_sync);
        res = local_res;
        err = local_err;
        done = true;
        _sync_cv.broadcast ();
    });

    _sync.lock ();
    while (!done)
        _sync_cv.wait (&_sync, -1);
    _sync.unlock ();

    if (err_)
        *err_ = err;
    release_active ();
    return res;
}
}

#endif
