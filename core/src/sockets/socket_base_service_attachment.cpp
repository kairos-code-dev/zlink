/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_base.hpp"
#include "services/discovery/socket_discovery_attachment.hpp"
#include "utils/likely.hpp"

int zlink::socket_base_t::set_admission_state (zlink_admission_state_t state_)
{
    if (state_ != ZLINK_ADMISSION_SERVING && state_ != ZLINK_ADMISSION_DRAINING) {
        errno = EINVAL;
        return -1;
    }

    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    {
        socket_public_api_lock_scope_t guard (lifecycle);
        if (_local_admission_state == state_)
            return 0;
        _local_admission_state = state_;
        if (_service_attachment)
            _service_attachment->on_local_admission_state_changed ();
        xlocal_admission_state_changed ();
    }
    return 0;
}

int zlink::socket_base_t::get_admission_state (
  zlink_admission_state_t *state_out_) const
{
    if (!state_out_) {
        errno = EFAULT;
        return -1;
    }

    socket_lifecycle_coordinator_t &lifecycle =
      const_cast<socket_base_t *> (this)->lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    socket_public_api_lock_scope_t guard (lifecycle);
    *state_out_ = _local_admission_state;
    return 0;
}

zlink_admission_state_t zlink::socket_base_t::local_admission_state () const
{
    return _local_admission_state;
}

int zlink::socket_base_t::close ()
{
    if (_service_attachment && _service_attachment->on_public_close () != 0)
        return -1;

    const bool from_self_callback =
      socket_send_ready_dispatch_scope_t::dispatching_socket (this);
    if (!lifecycle_coordinator ().begin_close_or_fail_busy (from_self_callback))
        return -1;
    if (from_self_callback)
        return 0;

    finish_close_handoff ();
    return 0;
}

int zlink::socket_base_t::attach_discovery (discovery_t *discovery_)
{
    if (!_service_attachment) {
        _service_attachment = new (std::nothrow)
          socket_discovery_attachment_t (this);
        if (!_service_attachment) {
            errno = ENOMEM;
            return -1;
        }
    }
    return _service_attachment->attach (discovery_);
}
