/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SOCKET_RUNTIME_HPP_INCLUDED__
#define __ZLINK_SOCKET_RUNTIME_HPP_INCLUDED__

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>

#include "core/endpoint.hpp"
#include "core/own.hpp"
#include "core/pipe.hpp"
#include "core/thread.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "utils/mutex.hpp"
#include "zlink.h"

namespace zlink
{
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

struct socket_endpoint_registry_t
{
    socket_endpoints_t endpoints;
    socket_inprocs_t inprocs;
};

struct socket_monitor_bridge_t
{
    socket_monitor_bridge_t () :
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
        public_api_state (0),
        public_api_sync (),
        callback_api_depth (0),
        close_deferred (false),
        send_ready_handler (NULL),
        send_ready_handler_subject (NULL),
        send_ready_handler_userdata (NULL),
        send_ready_seq (0),
        send_ready_armed (false),
        last_recv_source_rid (),
        last_recv_source_rid_valid (false)
    {
    }

    std::atomic<zlink_socket_msg_handler_fn> socket_msg_handler;
    std::atomic<void *> socket_msg_handler_subject;
    std::atomic<void *> socket_msg_handler_userdata;
    std::atomic<zlink_subscribe_handler_fn> spot_handler;
    std::atomic<void *> spot_handler_userdata;
    std::atomic<uint32_t> public_api_state;
    std::atomic<bool> public_api_sync;
    std::atomic<uint32_t> callback_api_depth;
    std::atomic<bool> close_deferred;
    std::atomic<zlink_send_ready_handler_fn> send_ready_handler;
    std::atomic<void *> send_ready_handler_subject;
    std::atomic<void *> send_ready_handler_userdata;
    std::atomic<uint32_t> send_ready_seq;
    mutex_t send_ready_writer_sync;
    std::atomic<bool> send_ready_armed;
    std::recursive_mutex socket_msg_dispatch_sync;
    zlink_routing_id_t last_recv_source_rid;
    bool last_recv_source_rid_valid;
};

struct socket_lifecycle_hooks_t
{
    socket_lifecycle_hooks_t () :
        mailbox_refcnt (0),
        destroy_pending (false),
        monitor_async_mailbox_owned (false),
        async_mailbox_active (false),
        async_quiesce_pending (false),
        async_processing_done (true)
    {
    }

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
    socket_endpoint_registry_t endpoint_registry;
    socket_monitor_bridge_t monitor_bridge;
    socket_dispatch_bridge_t dispatch_bridge;
    socket_lifecycle_hooks_t lifecycle_hooks;
};
}

#endif
