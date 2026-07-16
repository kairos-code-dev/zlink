/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"

#include "utils/err.hpp"

#include <string.h>

using namespace zlink::mesh;

namespace zlink
{
namespace mesh
{
void track_monitor (monitor_state_t *monitor_, bool live_);
}
}

void *zlink_mesh_node_monitor_open (void *mesh_node_,
                                    const zlink_mesh_monitor_open_options_t *options_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    if (check_versioned (options_) != 0)
        return NULL;

    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->monitor) {
        errno = EBUSY;
        return NULL;
    }
    monitor_state_t *monitor = new (std::nothrow) monitor_state_t ();
    if (!monitor) {
        errno = ENOMEM;
        return NULL;
    }
    monitor->node = node;
    monitor->mask = options_->events;
    node->monitor = monitor;
    node->monitor_count += 1;
    track_monitor (monitor, true);
    return monitor;
}

zlink_handler_result_t zlink_mesh_node_monitor_handler (void *monitor_,
                                                        zlink_mesh_monitor_handler_fn handler_,
                                                        void *userdata_)
{
    monitor_state_t *monitor = as_monitor (monitor_);
    if (!monitor) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (monitor->mutex);
    if (monitor->handler_active) {
        errno = EDEADLK;
        return ZLINK_HANDLER_DEADLOCK;
    }
    monitor->handler = handler_;
    monitor->handler_userdata = userdata_;
    return ZLINK_HANDLER_OK;
}

zlink_recv_result_t zlink_mesh_node_monitor_recv (void *monitor_,
                                                  zlink_mesh_monitor_event_t *event_out_,
                                                  zlink_recv_flags_t flags_)
{
    monitor_state_t *monitor = as_monitor (monitor_);
    if (!monitor) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (check_versioned (event_out_) != 0)
        return ZLINK_RECV_INVALID_STATE;

    std::unique_lock<std::mutex> lock (monitor->mutex);
    if (monitor->handler) {
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }
    while (monitor->events.empty ()) {
        if (flags_ & ZLINK_RECV_FLAGS_DONTWAIT) {
            errno = EAGAIN;
            return ZLINK_RECV_NO_DATA;
        }
        if (monitor->closed) {
            errno = ESHUTDOWN;
            return ZLINK_RECV_INVALID_STATE;
        }
        monitor->cv.wait_for (lock, std::chrono::milliseconds (100));
    }
    *event_out_ = monitor->events.front ();
    monitor->events.pop_front ();
    return ZLINK_RECV_OK;
}

zlink_config_result_t zlink_mesh_node_monitor_status (void *monitor_,
                                                      zlink_mesh_monitor_status_t *status_out_)
{
    monitor_state_t *monitor = as_monitor (monitor_);
    if (!monitor) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (check_versioned (status_out_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    mesh_node_t *node = monitor->node;
    std::lock_guard<std::mutex> node_lock (node->mutex);
    std::lock_guard<std::mutex> lock (monitor->mutex);
    zlink_mesh_monitor_status_t out = monitor->counters;
    out.struct_size = sizeof (out);
    out.version = 1;
    out.state = node->state;
    uint64_t active_claims = 0;
    for (std::map<owner_id_t, owner_state_t>::const_iterator it = node->owners.begin ();
         it != node->owners.end (); ++it) {
        if (it->second.domains[0].claimed)
            ++active_claims;
        if (it->second.domains[1].claimed)
            ++active_claims;
        out.pending_application_messages += it->second.domains[0].pending_messages;
        out.pending_infrastructure_messages += it->second.domains[1].pending_messages;
        out.pending_bytes +=
          it->second.domains[0].pending_bytes + it->second.domains[1].pending_bytes;
    }
    out.active_claims = active_claims;
    *status_out_ = out;
    return ZLINK_CONFIG_OK;
}

zlink_close_result_t zlink_mesh_node_monitor_close (void **monitor_p_)
{
    if (!monitor_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    monitor_state_t *monitor = as_monitor (*monitor_p_);
    if (!monitor) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    mesh_node_t *node = monitor->node;
    {
        std::lock_guard<std::mutex> node_lock (node->mutex);
        std::lock_guard<std::mutex> lock (monitor->mutex);
        if (monitor->handler_active) {
            errno = EDEADLK;
            return ZLINK_CLOSE_BUSY;
        }
        monitor->closed = true;
        node->monitor = NULL;
        if (node->monitor_count > 0)
            node->monitor_count -= 1;
    }
    track_monitor (monitor, false);
    delete monitor;
    *monitor_p_ = NULL;
    return ZLINK_CLOSE_OK;
}
