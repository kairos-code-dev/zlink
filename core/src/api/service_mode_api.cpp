/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include "services/spot/spot_node_access.hpp"

namespace
{
static int transition_service_to_callback_mode (
  zlink::service_mode_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if (state_->receive_callback_active || state_->pollin_refs > 0) {
        errno = EBUSY;
        return -1;
    }
    state_->receive_callback_active = true;
    return 0;
}

static void revert_service_receive_callback_mode (
  zlink::service_mode_state_t *state_)
{
    if (state_)
        state_->receive_callback_active = false;
}

static int activate_service_send_ready_mode (zlink::service_mode_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if (!state_->send_ready_active && state_->pollout_refs > 0) {
        errno = EBUSY;
        return -1;
    }
    state_->send_ready_active = true;
    return 0;
}

static void revert_service_send_ready_mode (zlink::service_mode_state_t *state_)
{
    if (state_)
        state_->send_ready_active = false;
}

static int ensure_service_recv_model (const zlink::service_mode_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if (state_->receive_callback_active) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

static int increment_service_poller_refs (zlink::service_mode_state_t *state_,
                                          short events_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if ((events_ & ZLINK_POLLIN) != 0 && state_->receive_callback_active) {
        errno = EBUSY;
        return -1;
    }
    if ((events_ & ZLINK_POLLOUT) != 0 && state_->send_ready_active) {
        errno = EBUSY;
        return -1;
    }
    if ((events_ & ZLINK_POLLIN) != 0)
        ++state_->pollin_refs;
    if ((events_ & ZLINK_POLLOUT) != 0)
        ++state_->pollout_refs;
    return 0;
}

static void decrement_service_poller_refs (zlink::service_mode_state_t *state_,
                                           short events_)
{
    if (!state_)
        return;
    if ((events_ & ZLINK_POLLIN) != 0 && state_->pollin_refs > 0)
        --state_->pollin_refs;
    if ((events_ & ZLINK_POLLOUT) != 0 && state_->pollout_refs > 0)
        --state_->pollout_refs;
}

static zlink::service_mode_state_t *spot_mode_state (spot_handle_t *spot_)
{
    return spot_ ? &spot_->mode_state : NULL;
}

static zlink::service_mode_state_t *spot_node_mode_state (
  zlink::spot_node_t *node_)
{
    return node_ ? &node_->mode_state () : NULL;
}
}

bool is_registered_spot_node_handle (void *node_)
{
    return zlink::spot_node_access_t::from_handle (node_) != NULL;
}

bool is_registered_spot_handle (void *spot_)
{
    return as_spot_handle (spot_) != NULL;
}

void register_spot_node_mode_state (zlink::spot_node_t *node_)
{
    LIBZLINK_UNUSED (node_);
}

void register_spot_mode_state (spot_handle_t *spot_)
{
    LIBZLINK_UNUSED (spot_);
}

void erase_spot_node_mode_state (zlink::spot_node_t *node_)
{
    LIBZLINK_UNUSED (node_);
}

void erase_spot_mode_state (spot_handle_t *spot_)
{
    LIBZLINK_UNUSED (spot_);
}

int spot_node_transition_to_callback_mode (zlink::spot_node_t *node_)
{
    zlink::service_mode_state_t *state = spot_node_mode_state (node_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }
    zlink::scoped_lock_t lock (state->sync);
    return transition_service_to_callback_mode (state);
}

void spot_node_revert_callback_transition (zlink::spot_node_t *node_)
{
    zlink::service_mode_state_t *state = spot_node_mode_state (node_);
    if (!state)
        return;
    zlink::scoped_lock_t lock (state->sync);
    revert_service_receive_callback_mode (state);
}

int spot_transition_to_callback_mode (spot_handle_t *spot_)
{
    zlink::service_mode_state_t *state = spot_mode_state (spot_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }
    zlink::scoped_lock_t lock (state->sync);
    return transition_service_to_callback_mode (state);
}

void spot_revert_callback_transition (spot_handle_t *spot_)
{
    zlink::service_mode_state_t *state = spot_mode_state (spot_);
    if (!state)
        return;
    zlink::scoped_lock_t lock (state->sync);
    revert_service_receive_callback_mode (state);
}

int spot_node_require_recv_model (zlink::spot_node_t *node_)
{
    zlink::service_mode_state_t *state = spot_node_mode_state (node_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }
    zlink::scoped_lock_t lock (state->sync);
    return ensure_service_recv_model (state);
}

int spot_require_recv_model (spot_handle_t *spot_)
{
    zlink::service_mode_state_t *state = spot_mode_state (spot_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }
    if (in_spot_dispatch_event_callback (spot_))
        return 0;
    zlink::scoped_lock_t lock (state->sync);
    return ensure_service_recv_model (state);
}

int spot_activate_send_ready_mode (spot_handle_t *spot_,
                                   bool *already_active_out_)
{
    zlink::service_mode_state_t *state = spot_mode_state (spot_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }
    zlink::scoped_lock_t lock (state->sync);
    if (already_active_out_)
        *already_active_out_ = state->send_ready_active;
    return activate_service_send_ready_mode (state);
}

void spot_revert_send_ready_mode (spot_handle_t *spot_)
{
    zlink::service_mode_state_t *state = spot_mode_state (spot_);
    if (!state)
        return;
    zlink::scoped_lock_t lock (state->sync);
    revert_service_send_ready_mode (state);
}

int spot_node_activate_send_ready_mode (zlink::spot_node_t *node_,
                                        bool *already_active_out_)
{
    zlink::service_mode_state_t *state = spot_node_mode_state (node_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }
    zlink::scoped_lock_t lock (state->sync);
    if (already_active_out_)
        *already_active_out_ = state->send_ready_active;
    return activate_service_send_ready_mode (state);
}

void spot_node_revert_send_ready_mode (zlink::spot_node_t *node_)
{
    zlink::service_mode_state_t *state = spot_node_mode_state (node_);
    if (!state)
        return;
    zlink::scoped_lock_t lock (state->sync);
    revert_service_send_ready_mode (state);
}

int increment_spot_node_poller_ref (zlink::spot_node_t *node_)
{
    return increment_spot_node_poller_ref (node_, ZLINK_POLLIN | ZLINK_POLLOUT);
}

int increment_spot_node_poller_ref (zlink::spot_node_t *node_, short events_)
{
    zlink::service_mode_state_t *state = spot_node_mode_state (node_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }
    zlink::scoped_lock_t lock (state->sync);
    return increment_service_poller_refs (state, events_);
}

void decrement_spot_node_poller_ref (zlink::spot_node_t *node_)
{
    decrement_spot_node_poller_ref (node_, ZLINK_POLLIN | ZLINK_POLLOUT);
}

void decrement_spot_node_poller_ref (zlink::spot_node_t *node_, short events_)
{
    zlink::service_mode_state_t *state = spot_node_mode_state (node_);
    if (!state)
        return;
    zlink::scoped_lock_t lock (state->sync);
    decrement_service_poller_refs (state, events_);
}

int increment_spot_poller_ref (spot_handle_t *spot_)
{
    return increment_spot_poller_ref (spot_, ZLINK_POLLIN | ZLINK_POLLOUT);
}

int increment_spot_poller_ref (spot_handle_t *spot_, short events_)
{
    zlink::service_mode_state_t *state = spot_mode_state (spot_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }
    zlink::scoped_lock_t lock (state->sync);
    return increment_service_poller_refs (state, events_);
}

void decrement_spot_poller_ref (spot_handle_t *spot_)
{
    decrement_spot_poller_ref (spot_, ZLINK_POLLIN | ZLINK_POLLOUT);
}

void decrement_spot_poller_ref (spot_handle_t *spot_, short events_)
{
    zlink::service_mode_state_t *state = spot_mode_state (spot_);
    if (!state)
        return;
    zlink::scoped_lock_t lock (state->sync);
    decrement_service_poller_refs (state, events_);
}
