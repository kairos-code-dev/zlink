/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_RUNTIME_BASE_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_RUNTIME_BASE_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "services/common/service_socket_registry.hpp"
#include "utils/mutex.hpp"

namespace zlink
{
enum service_lifecycle_state_t
{
    service_state_idle = 0,
    service_state_starting = 1,
    service_state_running = 2,
    service_state_stopping = 3,
    service_state_stopped = 4,
    service_state_faulted = 5
};

class service_runtime_base_t
{
  public:
    explicit service_runtime_base_t (ctx_t *ctx_ = NULL);

    void set_ctx (ctx_t *ctx_);
    bool transition_to (service_lifecycle_state_t target_);
    service_lifecycle_state_t state () const;
    bool is_running () const;
    bool is_stopping () const;
    bool is_stopped () const;
    void mark_faulted (int err_);
    int fault_errno () const;
    void register_socket (socket_base_t *socket_);
    void unregister_socket (const socket_base_t *socket_);
    int close_socket (socket_base_t *&socket_, int timeout_ms_ = 10000);
    int close_socket_and_wait (socket_base_t *&socket_, int timeout_ms_ = 10000);
    int wait_drained (int timeout_ms_);
    int force_wait_remaining (int timeout_ms_);
    size_t owned_socket_count () const;
    bool owns_socket (const socket_base_t *socket_) const;
    void clear_tracked_sockets ();

  private:
    ctx_t *_ctx;
    service_lifecycle_state_t _state;
    int _fault_errno;
    mutable mutex_t _sync;
    service_socket_registry_t _sockets;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (service_runtime_base_t)
};
}

#endif
