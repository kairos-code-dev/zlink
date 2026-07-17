/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/common/socket_base.hpp"

std::shared_ptr<zlink::reqrep_internal::router_request_reply_state_t>
zlink::socket_base_t::router_request_reply_state () const
{
    return _request_reply_bridge.router_request_reply_state;
}

void zlink::socket_base_t::set_router_request_reply_state (
  const std::shared_ptr<zlink::reqrep_internal::router_request_reply_state_t> &state_)
{
    _request_reply_bridge.router_request_reply_state = state_;
}

void zlink::socket_base_t::clear_router_request_reply_state ()
{
    _request_reply_bridge.router_request_reply_state.reset ();
}

std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
zlink::socket_base_t::request_reply_state () const
{
    return _request_reply_bridge.request_reply_state;
}

void zlink::socket_base_t::set_request_reply_state (
  const std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t> &state_)
{
    _request_reply_bridge.request_reply_state = state_;
}

void zlink::socket_base_t::clear_request_reply_state ()
{
    _request_reply_bridge.request_reply_state.reset ();
}

std::shared_ptr<zlink::part_helper_internal::handle_state_t>
zlink::socket_base_t::part_helper_state () const
{
    return std::atomic_load_explicit (&_request_reply_bridge.part_helper_state,
                                      std::memory_order_acquire);
}

std::shared_ptr<zlink::part_helper_internal::handle_state_t>
zlink::socket_base_t::set_part_helper_state (
  const std::shared_ptr<zlink::part_helper_internal::handle_state_t> &state_)
{
    std::shared_ptr<part_helper_internal::handle_state_t> expected;
    if (std::atomic_compare_exchange_strong_explicit (
          &_request_reply_bridge.part_helper_state, &expected, state_, std::memory_order_acq_rel,
          std::memory_order_acquire))
        return state_;
    return expected;
}

void zlink::socket_base_t::clear_part_helper_state ()
{
    std::atomic_store_explicit (
      &_request_reply_bridge.part_helper_state,
      std::shared_ptr<part_helper_internal::handle_state_t> (), std::memory_order_release);
}

int zlink::socket_base_t::set_channel_name_metadata (const char *channel_name_)
{
    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;
    socket_public_api_lock_scope_t guard (lifecycle);

    if (!channel_name_ || channel_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (std::strlen (channel_name_) > 255) {
        errno = EINVAL;
        return -1;
    }
    if (_channel_name_locked) {
        errno = EBUSY;
        return -1;
    }

    _channel_name.assign (channel_name_);
    return 0;
}

int zlink::socket_base_t::get_channel_name_metadata (char *channel_name_buf_,
                                                     size_t channel_name_capacity_,
                                                     size_t *channel_name_len_out_) const
{
    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;
    socket_public_api_lock_scope_t guard (lifecycle);

    if (!channel_name_len_out_) {
        errno = EFAULT;
        return -1;
    }
    if (_channel_name.empty ()) {
        errno = ENOENT;
        return -1;
    }

    *channel_name_len_out_ = _channel_name.size ();
    if (!channel_name_buf_)
        return 0;
    if (channel_name_capacity_ < _channel_name.size ()) {
        errno = EMSGSIZE;
        return -1;
    }
    if (!_channel_name.empty ()) {
        memcpy (channel_name_buf_, _channel_name.data (), _channel_name.size ());
    }
    return 0;
}

int zlink::socket_base_t::ensure_channel_name_metadata (const char *channel_name_)
{
    socket_public_api_lock_scope_t guard (lifecycle_coordinator ());

    if (!channel_name_ || channel_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (_channel_name.empty ()) {
        _channel_name.assign (channel_name_);
        return 0;
    }
    if (_channel_name != channel_name_) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void zlink::socket_base_t::lock_channel_name_metadata ()
{
    socket_public_api_lock_scope_t guard (lifecycle_coordinator ());
    _channel_name_locked = true;
}

bool zlink::socket_base_t::has_channel_name_metadata () const
{
    return !_channel_name.empty ();
}
