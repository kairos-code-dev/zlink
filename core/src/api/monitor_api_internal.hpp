/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_MONITOR_API_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_MONITOR_API_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

#include <atomic>

#include "sockets/socket_base.hpp"
#include "utils/mutex.hpp"

namespace zlink
{
class spot_node_t;
class spot_pub_t;
class spot_sub_t;
class spot_internal_receiver_t;
}

typedef int (*monitor_snapshot_provider_fn) (void *subject_,
                                             zlink_monitor_snapshot_t *out_);

struct monitor_handler_state_t
{
    monitor_handler_state_t (zlink::socket_base_t *socket_, bool service_) :
        socket (socket_),
        socket_handler (NULL),
        service_handler (NULL),
        socket_handler_userdata (NULL),
        service_handler_userdata (NULL),
        snapshot_provider (NULL),
        snapshot_subject (NULL),
        stop (false),
        callback_depth (0),
        close_requested (false),
        service (service_),
        dispatch_task_id (0)
    {
    }

    zlink::socket_base_t *socket;
    std::atomic<zlink_monitor_handler_fn> socket_handler;
    std::atomic<zlink_service_monitor_handler_fn> service_handler;
    std::atomic<void *> socket_handler_userdata;
    std::atomic<void *> service_handler_userdata;
    std::atomic<monitor_snapshot_provider_fn> snapshot_provider;
    std::atomic<void *> snapshot_subject;
    std::atomic<bool> stop;
    std::atomic<int> callback_depth;
    std::atomic<bool> close_requested;
    zlink::mutex_t dispatch_sync;
    bool service;
    uint64_t dispatch_task_id;
};

extern thread_local monitor_handler_state_t *g_current_monitor_handler_state;

monitor_handler_state_t *find_monitor_handler_state (
  zlink::socket_base_t *socket_);
bool has_open_service_monitor_for_subject (void *snapshot_subject_);
bool has_open_spot_node_monitor_child (zlink::spot_node_t *node_);
bool in_spot_node_monitor_callback (zlink::spot_node_t *node_);
zlink::socket_base_t *raw_monitor_snapshot_subject (
  monitor_handler_state_t *state_);
void clear_raw_monitor_snapshot_subjects (zlink::socket_base_t *source_);
void unregister_monitor_handlers (zlink::socket_base_t *socket_);

int set_monitor_handler_state (zlink::socket_base_t *socket_,
                               zlink_monitor_handler_fn socket_handler_,
                               zlink_service_monitor_handler_fn service_handler_,
                               bool service_,
                               monitor_snapshot_provider_fn snapshot_provider_,
                               void *snapshot_subject_,
                               void *socket_handler_userdata_,
                               void *service_handler_userdata_);

int socket_monitor_snapshot_provider (void *subject_,
                                      zlink_monitor_snapshot_t *out_);
int spot_pub_monitor_snapshot_provider (void *subject_,
                                        zlink_monitor_snapshot_t *out_);
int spot_sub_monitor_snapshot_provider (void *subject_,
                                        zlink_monitor_snapshot_t *out_);
int spot_internal_receiver_monitor_snapshot_provider (
  void *subject_,
  zlink_monitor_snapshot_t *out_);

int recv_socket_monitor_event_unchecked (void *monitor_socket_,
                                         zlink_monitor_event_t *event_,
                                         int flags_);
int recv_service_monitor_event_unchecked (void *monitor_,
                                          zlink_service_event_t *event_,
                                          int flags_);
int require_monitor_recv_model (void *monitor_, bool service_);

#endif
