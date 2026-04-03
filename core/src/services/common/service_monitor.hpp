/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_MONITOR_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_MONITOR_HPP_INCLUDED__

#include <zlink.h>

#include "core/ctx.hpp"
#include "utils/stdint.hpp"
#include "sockets/socket_base.hpp"
#include "utils/macros.hpp"
#include "utils/mutex.hpp"

#include <atomic>
#include <deque>
#include <string>
#include <vector>

namespace zlink
{
class service_monitor_hub_t
{
  public:
    explicit service_monitor_hub_t (ctx_t *ctx_);
    ~service_monitor_hub_t ();

    void *open (int events_);
    void emit (const zlink_service_event_t &event_);
    void emit_batch (const zlink_service_event_t *events_, size_t count_);
    void close_all (const zlink_service_event_t *terminal_event_ = NULL);
    bool has_watchers () const;
    size_t watcher_count () const;
    void prune_closed_watchers ();

  private:
    struct watcher_t
    {
        socket_base_t *server;
        uint32_t events;
        std::string endpoint;

        watcher_t () : server (NULL), events (0) {}
    };

    static uint32_t event_delivery_mask (const zlink_service_event_t &event_);
    static void dispatch_task_main (void *arg_);
    void dispatch_pending ();
    void dispatch_event (const zlink_service_event_t &event_);
    void ensure_dispatch_task_started ();
    void stop_dispatch_task ();

    ctx_t *_ctx;
    mutex_t _sync;
    std::vector<watcher_t> _watchers;
    std::atomic<size_t> _watcher_count;
    uint32_t _next_id;
    mutex_t _dispatch_sync;
    std::deque<zlink_service_event_t> _dispatch_queue;
    uint64_t _dispatch_task_id;
    bool _dispatch_task_running;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (service_monitor_hub_t)
};
}

#endif
