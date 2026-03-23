/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

namespace
{
struct service_mode_state_t
{
    service_mode_state_t () :
        receive_callback_active (false),
        send_ready_active (false),
        pollin_refs (0),
        pollout_refs (0)
    {
    }

    bool receive_callback_active;
    bool send_ready_active;
    int pollin_refs;
    int pollout_refs;
};

struct gateway_mode_registry_t
{
    zlink::mutex_t sync;
    std::map<void *, service_mode_state_t> states;
};

struct spot_node_mode_registry_t
{
    zlink::mutex_t sync;
    std::map<void *, service_mode_state_t> states;
};

struct spot_mode_registry_t
{
    zlink::mutex_t sync;
    std::map<void *, service_mode_state_t> states;
};

static gateway_mode_registry_t &gateway_mode_registry ()
{
    static gateway_mode_registry_t registry;
    return registry;
}

static spot_node_mode_registry_t &spot_node_mode_registry ()
{
    static spot_node_mode_registry_t registry;
    return registry;
}

static spot_mode_registry_t &spot_mode_registry ()
{
    static spot_mode_registry_t registry;
    return registry;
}

static int transition_service_to_callback_mode (service_mode_state_t *state_)
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

static void revert_service_receive_callback_mode (service_mode_state_t *state_)
{
    if (state_)
        state_->receive_callback_active = false;
}

static int activate_service_send_ready_mode (service_mode_state_t *state_)
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

static void revert_service_send_ready_mode (service_mode_state_t *state_)
{
    if (state_)
        state_->send_ready_active = false;
}

static int ensure_service_recv_model (const service_mode_state_t *state_)
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

static int increment_service_poller_refs (service_mode_state_t *state_,
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

static void decrement_service_poller_refs (service_mode_state_t *state_,
                                           short events_)
{
    if (!state_)
        return;
    if ((events_ & ZLINK_POLLIN) != 0 && state_->pollin_refs > 0)
        --state_->pollin_refs;
    if ((events_ & ZLINK_POLLOUT) != 0 && state_->pollout_refs > 0)
        --state_->pollout_refs;
}
}

bool is_registered_gateway_handle (void *gateway_)
{
    if (!gateway_)
        return false;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return registry.states.find (gateway_) != registry.states.end ();
}

bool is_registered_spot_node_handle (void *node_)
{
    if (!node_)
        return false;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return registry.states.find (node_) != registry.states.end ();
}

bool is_registered_spot_handle (void *spot_)
{
    if (!spot_)
        return false;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return registry.states.find (spot_) != registry.states.end ();
}

void register_gateway_mode_state (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states[gateway_] = service_mode_state_t ();
}

void register_spot_node_mode_state (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states[node_] = service_mode_state_t ();
}

void register_spot_mode_state (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states[spot_] = service_mode_state_t ();
}

void erase_gateway_mode_state (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states.erase (gateway_);
}

void erase_spot_node_mode_state (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states.erase (node_);
}

void erase_spot_mode_state (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states.erase (spot_);
}

int gateway_transition_to_callback_mode (zlink::gateway_t *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return transition_service_to_callback_mode (&registry.states[gateway_]);
}

void gateway_revert_callback_transition (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_receive_callback_mode (&registry.states[gateway_]);
}

int spot_node_transition_to_callback_mode (zlink::spot_node_t *node_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return transition_service_to_callback_mode (&registry.states[node_]);
}

void spot_node_revert_callback_transition (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_receive_callback_mode (&registry.states[node_]);
}

int spot_transition_to_callback_mode (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return transition_service_to_callback_mode (&registry.states[spot_]);
}

void spot_revert_callback_transition (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_receive_callback_mode (&registry.states[spot_]);
}

int gateway_require_recv_model (zlink::gateway_t *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_recv_model (&registry.states[gateway_]);
}

int spot_node_require_recv_model (zlink::spot_node_t *node_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_recv_model (&registry.states[node_]);
}

int spot_require_recv_model (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_recv_model (&registry.states[spot_]);
}

int gateway_activate_send_ready_mode (zlink::gateway_t *gateway_,
                                      bool *already_active_out_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    if (already_active_out_)
        *already_active_out_ = registry.states[gateway_].send_ready_active;
    return activate_service_send_ready_mode (&registry.states[gateway_]);
}

void gateway_revert_send_ready_mode (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_send_ready_mode (&registry.states[gateway_]);
}

int spot_activate_send_ready_mode (spot_handle_t *spot_,
                                   bool *already_active_out_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    if (already_active_out_)
        *already_active_out_ = registry.states[spot_].send_ready_active;
    return activate_service_send_ready_mode (&registry.states[spot_]);
}

void spot_revert_send_ready_mode (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_send_ready_mode (&registry.states[spot_]);
}

int spot_node_activate_send_ready_mode (zlink::spot_node_t *node_,
                                        bool *already_active_out_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    if (already_active_out_)
        *already_active_out_ = registry.states[node_].send_ready_active;
    return activate_service_send_ready_mode (&registry.states[node_]);
}

void spot_node_revert_send_ready_mode (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_send_ready_mode (&registry.states[node_]);
}

int increment_gateway_poller_ref (zlink::gateway_t *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[gateway_],
                                          ZLINK_POLLIN | ZLINK_POLLOUT);
}

int increment_gateway_poller_ref (zlink::gateway_t *gateway_, short events_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[gateway_], events_);
}

void decrement_gateway_poller_ref (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[gateway_],
                                   ZLINK_POLLIN | ZLINK_POLLOUT);
}

void decrement_gateway_poller_ref (zlink::gateway_t *gateway_, short events_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[gateway_], events_);
}

int increment_spot_node_poller_ref (zlink::spot_node_t *node_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[node_],
                                          ZLINK_POLLIN | ZLINK_POLLOUT);
}

int increment_spot_node_poller_ref (zlink::spot_node_t *node_, short events_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[node_], events_);
}

void decrement_spot_node_poller_ref (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[node_],
                                   ZLINK_POLLIN | ZLINK_POLLOUT);
}

void decrement_spot_node_poller_ref (zlink::spot_node_t *node_, short events_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[node_], events_);
}

int increment_spot_poller_ref (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[spot_],
                                          ZLINK_POLLIN | ZLINK_POLLOUT);
}

int increment_spot_poller_ref (spot_handle_t *spot_, short events_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[spot_], events_);
}

void decrement_spot_poller_ref (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[spot_],
                                   ZLINK_POLLIN | ZLINK_POLLOUT);
}

void decrement_spot_poller_ref (spot_handle_t *spot_, short events_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[spot_], events_);
}
