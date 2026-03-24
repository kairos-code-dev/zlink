/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SOCKET_RUNTIME_HPP_INCLUDED__
#define __ZLINK_SOCKET_RUNTIME_HPP_INCLUDED__

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>

#include "core/endpoint.hpp"
#include "core/mailbox.hpp"
#include "core/own.hpp"
#include "core/pipe.hpp"
#include "core/thread.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "utils/mutex.hpp"
#include "zlink.h"

namespace zlink
{
class io_thread_t;
class mailbox_t;

enum
{
    socket_monitor_max_values = 4
};

struct socket_monitor_event_record_t
{
    socket_monitor_event_record_t () :
        event (0),
        values_count (0)
    {
        memset (values, 0, sizeof (values));
        memset (&routing_id, 0, sizeof (routing_id));
    }

    uint64_t event;
    uint64_t values[socket_monitor_max_values];
    uint64_t values_count;
    zlink_routing_id_t routing_id;
    endpoint_uri_pair_t endpoint_uri_pair;
};

typedef std::pair<own_t *, pipe_t *> socket_endpoint_pipe_t;
typedef std::multimap<std::string, socket_endpoint_pipe_t> socket_endpoints_t;

class socket_inprocs_t
{
  public:
    void emplace (const char *endpoint_uri_, pipe_t *pipe_);
    int erase_pipes (const std::string &endpoint_uri_str_);
    void erase_pipe (const pipe_t *pipe_);

  private:
    typedef std::multimap<std::string, pipe_t *> map_t;
    map_t _inprocs;
};

struct socket_endpoint_runtime_t
{
    socket_endpoints_t endpoints;
    socket_inprocs_t inprocs;
    zlink_routing_id_t last_recv_source_rid;
    bool last_recv_source_rid_valid;

    socket_endpoint_runtime_t () :
        last_recv_source_rid (),
        last_recv_source_rid_valid (false)
    {
    }
};

struct socket_monitor_runtime_t
{
    socket_monitor_runtime_t () :
        socket (NULL),
        events (0),
        events_atomic (0),
        lossy (true),
        queue_stop (false),
        thread_started (false)
    {
    }

    void *socket;
    int64_t events;
    std::atomic<int64_t> events_atomic;
    bool lossy;
    mutex_t sync;
    mutex_t queue_sync;
    condition_variable_t queue_cv;
    std::deque<socket_monitor_event_record_t> queue;
    bool queue_stop;
    thread_t thread;
    bool thread_started;
};

struct socket_dispatch_bridge_t
{
    socket_dispatch_bridge_t () :
        socket_msg_handler (NULL),
        socket_msg_handler_subject (NULL),
        socket_msg_handler_userdata (NULL),
        spot_handler (NULL),
        spot_handler_userdata (NULL),
        send_ready_handler (NULL),
        send_ready_handler_subject (NULL),
        send_ready_handler_userdata (NULL),
        send_ready_seq (0),
        send_ready_armed (false)
    {
    }

    std::atomic<zlink_socket_msg_handler_fn> socket_msg_handler;
    std::atomic<void *> socket_msg_handler_subject;
    std::atomic<void *> socket_msg_handler_userdata;
    std::atomic<zlink_subscribe_handler_fn> spot_handler;
    std::atomic<void *> spot_handler_userdata;
    std::atomic<zlink_send_ready_handler_fn> send_ready_handler;
    std::atomic<void *> send_ready_handler_subject;
    std::atomic<void *> send_ready_handler_userdata;
    std::atomic<uint32_t> send_ready_seq;
    mutex_t send_ready_writer_sync;
    std::atomic<bool> send_ready_armed;
    std::recursive_mutex socket_msg_dispatch_sync;
};

class socket_lifecycle_coordinator_t
{
  public:
    socket_lifecycle_coordinator_t () :
        public_api_state (0),
        public_api_sync (),
        callback_api_depth (0),
        close_deferred (false),
        mailbox_refcnt (0),
        destroy_pending (false),
        monitor_async_mailbox_owned (false),
        async_mailbox_active (false),
        async_quiesce_pending (false),
        async_processing_done (true)
    {
    }

    bool enter_public_api ();
    void leave_public_api ();
    bool enter_callback_api ();
    bool leave_callback_api ();
    bool begin_close_or_fail_busy (bool from_self_callback_);
    bool public_close_requested () const;
    void lock_public_api_sync ();
    void unlock_public_api_sync ();

    int start_async_mailbox_processing (mailbox_t *mailbox_,
                                        io_thread_t *io_thread_,
                                        mailbox_t::mailbox_handler_t handler_,
                                        void *handler_arg_,
                                        mailbox_t::mailbox_pre_post_t pre_post_);
    void stop_async_mailbox_processing (mailbox_t *mailbox_);
    void mark_async_processing_stopped (mailbox_t *mailbox_);
    void wait_async_quiesced (int timeout_ms_);
    bool is_async_mailbox_active () const;
    bool is_async_quiesce_pending () const;
    void clear_deferred_close ();
    void set_monitor_async_mailbox_owned (bool owned_);
    bool is_monitor_async_mailbox_owned () const;
    void mark_destroy_pending ();
    void clear_destroy_pending ();
    bool is_destroy_pending () const;
    int mailbox_refcount ();
    void inc_mailbox_ref ();
    bool dec_mailbox_ref ();

    std::atomic<uint32_t> public_api_state;
    std::atomic<bool> public_api_sync;
    std::atomic<uint32_t> callback_api_depth;
    std::atomic<bool> close_deferred;
    atomic_counter_t mailbox_refcnt;
    bool destroy_pending;
    bool monitor_async_mailbox_owned;
    std::atomic<bool> async_mailbox_active;
    std::atomic<bool> async_quiesce_pending;
    std::atomic<bool> async_processing_done;
    mutex_t async_done_mu;
    condition_variable_t async_done_cv;
};

struct socket_runtime_t
{
    socket_endpoint_runtime_t endpoint_runtime;
    socket_monitor_runtime_t monitor_runtime;
    socket_dispatch_bridge_t dispatch_bridge;
    socket_lifecycle_coordinator_t lifecycle_coordinator;
};
}

#endif
