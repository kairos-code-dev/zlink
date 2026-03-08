/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_MONITOR_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_MONITOR_HPP_INCLUDED__

#include <zlink.h>

#include "core/ctx.hpp"
#include "sockets/socket_base.hpp"
#include "utils/macros.hpp"
#include "utils/mutex.hpp"

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
    void close_all (const zlink_service_event_t *terminal_event_ = NULL);

  private:
    struct watcher_t
    {
        socket_base_t *server;
        uint32_t events;
        std::string endpoint;

        watcher_t () : server (NULL), events (0) {}
    };

    static uint32_t event_delivery_mask (uint32_t event_type_);

    ctx_t *_ctx;
    mutex_t _sync;
    std::vector<watcher_t> _watchers;
    uint32_t _next_id;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (service_monitor_hub_t)
};
}

#endif
