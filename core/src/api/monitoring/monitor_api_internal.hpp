/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_MONITOR_API_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_MONITOR_API_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

#include <atomic>

#include "sockets/common/socket_base.hpp"
#include "utils/mutex.hpp"

typedef int (*monitor_snapshot_provider_fn) (void *subject_, zlink_monitor_status_t *out_);

struct monitor_handler_state_t
{
    monitor_handler_state_t (zlink::socket_base_t *socket_) :
        socket (socket_),
        socket_handler (NULL),
        socket_handler_userdata (NULL),
        snapshot_provider (NULL),
        snapshot_subject (NULL),
        stop (false),
        callback_depth (0),
        close_requested (false),
        dispatch_task_id (0)
    {
    }

    zlink::socket_base_t *socket;
    std::atomic<zlink_monitor_handler_fn> socket_handler;
    std::atomic<void *> socket_handler_userdata;
    std::atomic<monitor_snapshot_provider_fn> snapshot_provider;
    std::atomic<void *> snapshot_subject;
    std::atomic<bool> stop;
    std::atomic<int> callback_depth;
    std::atomic<bool> close_requested;
    zlink::mutex_t dispatch_sync;
    uint64_t dispatch_task_id;
};

namespace zlink
{
class monitor_dispatch_context_t
{
  public:
    explicit monitor_dispatch_context_t (monitor_handler_state_t *state_);
    ~monitor_dispatch_context_t ();

    static monitor_handler_state_t *current_state ();
    static void *current_handle ();

  private:
    monitor_handler_state_t *_previous_state;
};

monitor_handler_state_t *current_monitor_handler_state ();
void *current_monitor_dispatch_handle ();
}

monitor_handler_state_t *find_monitor_handler_state (zlink::socket_base_t *socket_);
zlink::socket_base_t *raw_monitor_snapshot_subject (monitor_handler_state_t *state_);
void clear_raw_monitor_snapshot_subjects (zlink::socket_base_t *source_);
void unregister_monitor_handlers (zlink::socket_base_t *socket_);

int set_monitor_handler_state (zlink::socket_base_t *socket_,
                               zlink_monitor_handler_fn socket_handler_,
                               monitor_snapshot_provider_fn snapshot_provider_,
                               void *snapshot_subject_,
                               void *socket_handler_userdata_);

int socket_monitor_snapshot_provider (void *subject_, zlink_monitor_status_t *out_);
int spot_pub_monitor_snapshot_provider (void *subject_, zlink_monitor_status_t *out_);
int spot_sub_monitor_snapshot_provider (void *subject_, zlink_monitor_status_t *out_);
int spot_internal_receiver_monitor_snapshot_provider (void *subject_, zlink_monitor_status_t *out_);

int recv_socket_monitor_event_unchecked (void *monitor_socket_,
                                         zlink_monitor_event_t *event_,
                                         int flags_);
int require_monitor_recv_model (void *monitor_);

#endif
