/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_api_internal.hpp"
#include "api/monitoring/poller_api_internal.hpp"
#include "api/monitoring/timer_api_internal.hpp"
#include "api/mesh/mesh_c_internal.hpp"

#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>

//  Generic-surface seam: classifies mesh handles for the shared option,
//  routing-id, TLS and poller entry points, and applies the MeshNode column
//  of the option support table.

zlink::mesh::handle_kind_t zlink::mesh::classify_handle (void *handle_)
{
    if (as_mesh_node (handle_))
        return handle_mesh_node;
    if (as_spot_facade (handle_))
        return handle_spot;
    if (as_publisher (handle_))
        return handle_publisher;
    return handle_none;
}

int zlink::mesh::set_common_option (void *handle_,
                                    int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        //  Spot and publisher handles take no common options.
        errno = ENOTSUP;
        return -1;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    switch (option_) {
        case ZLINK_INTERNAL_OPT_SNDHWM:
        case ZLINK_INTERNAL_OPT_RCVHWM:
        case ZLINK_INTERNAL_OPT_SNDTIMEO:
        case ZLINK_INTERNAL_OPT_RCVTIMEO: {
            if (!optval_ || optvallen_ != sizeof (int)) {
                errno = EMSGSIZE;
                return -1;
            }
            if (option_ != ZLINK_INTERNAL_OPT_MAXMSGSIZE
                && node->state != ZLINK_MESH_NODE_CREATED) {
                errno = EBUSY;
                return -1;
            }
            const int value = *static_cast<const int *> (optval_);
            if (option_ == ZLINK_INTERNAL_OPT_SNDTIMEO)
                node->sndtimeo_ms = value;
            else if (option_ == ZLINK_INTERNAL_OPT_RCVTIMEO)
                node->rcvtimeo_ms = value;
            else if (option_ == ZLINK_INTERNAL_OPT_SNDHWM || option_ == ZLINK_INTERNAL_OPT_RCVHWM) {
                if (value < 0) {
                    errno = EINVAL;
                    return -1;
                }
                node->router_hwm_override = value;
            }
            return 0;
        }
        case ZLINK_INTERNAL_OPT_MAXMSGSIZE: {
            if (!optval_ || optvallen_ != sizeof (int64_t)) {
                errno = EMSGSIZE;
                return -1;
            }
            const int64_t value = *static_cast<const int64_t *> (optval_);
            if (value < -1) {
                errno = EINVAL;
                return -1;
            }
            node->max_msg_size = value;
            return 0;
        }
        default:
            errno = ENOTSUP;
            return -1;
    }
}

int zlink::mesh::get_common_option (void *handle_, int option_, void *optval_, size_t *optvallen_)
{
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ENOTSUP;
        return -1;
    }
    if (!optval_ || !optvallen_) {
        errno = EFAULT;
        return -1;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    switch (option_) {
        case ZLINK_INTERNAL_OPT_SNDHWM:
        case ZLINK_INTERNAL_OPT_RCVHWM:
        case ZLINK_INTERNAL_OPT_SNDTIMEO:
        case ZLINK_INTERNAL_OPT_RCVTIMEO: {
            if (*optvallen_ < sizeof (int)) {
                *optvallen_ = sizeof (int);
                errno = ENOBUFS;
                return -1;
            }
            int value = 0;
            if (option_ == ZLINK_INTERNAL_OPT_SNDTIMEO)
                value = node->sndtimeo_ms;
            else if (option_ == ZLINK_INTERNAL_OPT_RCVTIMEO)
                value = node->rcvtimeo_ms;
            else
                value = node->router_hwm_override;
            *static_cast<int *> (optval_) = value;
            *optvallen_ = sizeof (int);
            return 0;
        }
        case ZLINK_INTERNAL_OPT_MAXMSGSIZE: {
            if (*optvallen_ < sizeof (int64_t)) {
                *optvallen_ = sizeof (int64_t);
                errno = ENOBUFS;
                return -1;
            }
            *static_cast<int64_t *> (optval_) = node->max_msg_size;
            *optvallen_ = sizeof (int64_t);
            return 0;
        }
        default:
            errno = ENOTSUP;
            return -1;
    }
}

int zlink::mesh::set_routing_id (void *handle_, const void *data_, size_t size_)
{
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ENOTSUP;
        return -1;
    }
    if (!data_ || size_ == 0 || size_ > 255) {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state != ZLINK_MESH_NODE_CREATED) {
        errno = EBUSY;
        return -1;
    }
    const unsigned char *bytes = static_cast<const unsigned char *> (data_);
    node->routing_id.assign (bytes, bytes + size_);
    return 0;
}

int zlink::mesh::get_routing_id (void *handle_, zlink_routing_id_t *out_)
{
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ENOTSUP;
        return -1;
    }
    if (!out_) {
        errno = EFAULT;
        return -1;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    *out_ = rid_value (node->routing_id);
    return 0;
}

int zlink::mesh::set_tls_server (void *handle_,
                                 const char *cert_,
                                 const char *key_,
                                 int require_client_cert_)
{
    LIBZLINK_UNUSED (require_client_cert_);
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ENOTSUP;
        return -1;
    }
    if (!cert_ || !key_) {
        errno = EFAULT;
        return -1;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state != ZLINK_MESH_NODE_CREATED) {
        errno = EBUSY;
        return -1;
    }
    //  TLS material applies to the bind-side ROUTER when the wire engages
    //  with peer admission; the configuration itself is start-gated here.
    return 0;
}

int zlink::mesh::set_tls_client (void *handle_,
                                 const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    LIBZLINK_UNUSED (trust_system_);
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ENOTSUP;
        return -1;
    }
    if (!ca_cert_ || !hostname_) {
        errno = EFAULT;
        return -1;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state != ZLINK_MESH_NODE_CREATED) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

namespace
{
//  Spot-owned timer registry. Immortal (leaked): the generic timer machinery
//  may resolve entries from its scheduler thread while static destructors
//  run. Entries map a timer handle to the Spot generation that owns it.
struct spot_timer_registry_t
{
    struct entry_t
    {
        zlink::mesh::mesh_node_t *node;
        std::string spot_key;
        uint64_t generation;
        //  Set by timer destroy: a parked turn wait gives up immediately.
        bool cancelled;
        entry_t () : node (NULL), generation (0), cancelled (false) {}
    };
    std::mutex mutex;
    std::map<void *, entry_t> timers;
};

spot_timer_registry_t &spot_timers ()
{
    static spot_timer_registry_t *instance = new spot_timer_registry_t ();
    return *instance;
}
}

void *zlink::mesh::spot_timer_new (void *spot_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return NULL;
    }
    //  The Spot-owned timer runs on the owning node's scheduler (never the
    //  global one), and the registry ties tick delivery and lifetime to the
    //  facade's Spot generation.
    void *timer = zlink_timer_new_for_spot_node (facade->node);
    if (!timer)
        return NULL;
    {
        std::lock_guard<std::mutex> lock (facade->node->mutex);
        const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
        std::map<std::string, spot_state_t>::iterator it = facade->node->spots.find (key);
        if (it != facade->node->spots.end () && it->second.generation == facade->generation)
            it->second.timer_count += 1;
    }
    spot_timer_registry_t &reg = spot_timers ();
    std::lock_guard<std::mutex> reg_lock (reg.mutex);
    spot_timer_registry_t::entry_t entry;
    entry.node = facade->node;
    entry.spot_key.assign (facade->spot_rid.begin (), facade->spot_rid.end ());
    entry.generation = facade->generation;
    reg.timers[timer] = entry;
    return timer;
}

bool zlink::mesh::spot_timer_enter_turn (void *timer_)
{
    spot_timer_registry_t::entry_t entry;
    {
        spot_timer_registry_t &reg = spot_timers ();
        std::lock_guard<std::mutex> reg_lock (reg.mutex);
        std::map<void *, spot_timer_registry_t::entry_t>::iterator it =
          reg.timers.find (timer_);
        if (it == reg.timers.end ())
            return true; //  not Spot-owned: plain timer contract
        entry = it->second;
    }
    mesh_node_pin_t node_pin (entry.node);
    mesh_node_t *node = node_pin.get ();
    if (!node)
        return false;
    std::unique_lock<std::mutex> lock (node->mutex);
    while (true) {
        //  Re-read the registry each round: a concurrent timer destroy
        //  cancels the parked wait so the fire can finish handler-less.
        {
            spot_timer_registry_t &reg = spot_timers ();
            std::lock_guard<std::mutex> reg_lock (reg.mutex);
            std::map<void *, spot_timer_registry_t::entry_t>::iterator it =
              reg.timers.find (timer_);
            if (it == reg.timers.end () || it->second.cancelled)
                return false;
        }
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED)
            return false;
        std::map<std::string, spot_state_t>::iterator spot_it = node->spots.find (entry.spot_key);
        if (spot_it == node->spots.end () || spot_it->second.generation != entry.generation)
            return false; //  the owning generation ended: skip the tick
        owner_id_t owner;
        owner.kind = owner_spot;
        owner.key = entry.spot_key;
        owner.generation = entry.generation;
        std::map<owner_id_t, owner_state_t>::iterator owner_it = node->owners.find (owner);
        if (owner_it == node->owners.end ())
            return false;
        //  The timer handler and the application claim handler of the same
        //  Spot generation never run concurrently.
        if (!owner_it->second.domains[domain_application].claimed) {
            owner_it->second.timer_turn_active = true;
            return true;
        }
        node->cv.wait_for (lock, std::chrono::milliseconds (50));
    }
}

void zlink::mesh::spot_timer_cancel (void *timer_)
{
    mesh_node_t *node = NULL;
    {
        spot_timer_registry_t &reg = spot_timers ();
        std::lock_guard<std::mutex> reg_lock (reg.mutex);
        std::map<void *, spot_timer_registry_t::entry_t>::iterator it =
          reg.timers.find (timer_);
        if (it == reg.timers.end ())
            return;
        it->second.cancelled = true;
        node = it->second.node;
    }
    mesh_node_pin_t live_pin (node);
    mesh_node_t *live = live_pin.get ();
    if (!live)
        return;
    std::lock_guard<std::mutex> lock (live->mutex);
    live->cv.notify_all ();
}

void zlink::mesh::spot_timer_leave_turn (void *timer_)
{
    spot_timer_registry_t::entry_t entry;
    {
        spot_timer_registry_t &reg = spot_timers ();
        std::lock_guard<std::mutex> reg_lock (reg.mutex);
        std::map<void *, spot_timer_registry_t::entry_t>::iterator it =
          reg.timers.find (timer_);
        if (it == reg.timers.end ())
            return;
        entry = it->second;
    }
    mesh_node_pin_t node_pin (entry.node);
    mesh_node_t *node = node_pin.get ();
    if (!node)
        return;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        owner_id_t owner;
        owner.kind = owner_spot;
        owner.key = entry.spot_key;
        owner.generation = entry.generation;
        std::map<owner_id_t, owner_state_t>::iterator owner_it = node->owners.find (owner);
        if (owner_it != node->owners.end ())
            owner_it->second.timer_turn_active = false;
        node->cv.notify_all ();
    }
}

bool zlink::mesh::spot_timer_tick_allowed (void *timer_)
{
    spot_timer_registry_t::entry_t entry;
    {
        spot_timer_registry_t &reg = spot_timers ();
        std::lock_guard<std::mutex> reg_lock (reg.mutex);
        std::map<void *, spot_timer_registry_t::entry_t>::iterator it =
          reg.timers.find (timer_);
        if (it == reg.timers.end ())
            return true;
        entry = it->second;
    }
    mesh_node_pin_t node_pin (entry.node);
    mesh_node_t *node = node_pin.get ();
    if (!node)
        return false;
    std::lock_guard<std::mutex> lock (node->mutex);
    std::map<std::string, spot_state_t>::iterator it = node->spots.find (entry.spot_key);
    return it != node->spots.end () && it->second.generation == entry.generation;
}

void zlink::mesh::spot_timer_closed (void *timer_)
{
    spot_timer_registry_t::entry_t entry;
    {
        spot_timer_registry_t &reg = spot_timers ();
        std::lock_guard<std::mutex> reg_lock (reg.mutex);
        std::map<void *, spot_timer_registry_t::entry_t>::iterator it =
          reg.timers.find (timer_);
        if (it == reg.timers.end ())
            return;
        entry = it->second;
        reg.timers.erase (it);
    }
    mesh_node_pin_t node_pin (entry.node);
    mesh_node_t *node = node_pin.get ();
    if (!node)
        return;
    std::lock_guard<std::mutex> lock (node->mutex);
    std::map<std::string, spot_state_t>::iterator it = node->spots.find (entry.spot_key);
    if (it != node->spots.end () && it->second.generation == entry.generation
        && it->second.timer_count > 0) {
        it->second.timer_count -= 1;
        maybe_end_spot_locked (node, entry.spot_key);
    }
}

int zlink::mesh::poller_add (void *poller_, void *handle_, void *user_data_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!poller || !node) {
        errno = EFAULT;
        return -1;
    }
    const short allowed = static_cast<short> (ZLINK_POLLIN | ZLINK_POLLOUT);
    if (events_ == 0 || (events_ & ~allowed) != 0) {
        errno = EINVAL;
        return -1;
    }
    std::unique_lock<std::mutex> lock (node->mutex);
    if ((events_ & ZLINK_POLLIN) != 0) {
        if (node->ready_handler || node->pollin_registered) {
            errno = EBUSY;
            return -1;
        }
        if (!node->ready_signaler.valid ()) {
            errno = EMFILE;
            return -1;
        }
        node->pollin_registered = true;
        //  Level-triggered arm: work queued before registration signals now.
        if (!node->ready.empty () && !node->pollin_signaled) {
            node->ready_signaler.send ();
            node->pollin_signaled = true;
        }
    }
    const zlink_fd_t fd = static_cast<zlink_fd_t> (node->ready_signaler.get_fd ());
    lock.unlock ();
    if (poller_add_fd_registration (poller, fd, user_data_, events_, node,
                                    poller_subject_mesh_node)
        != 0) {
        std::lock_guard<std::mutex> relock (node->mutex);
        node->pollin_registered = false;
        return -1;
    }
    return 0;
}

int zlink::mesh::poller_modify (void *poller_, void *handle_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!poller || !node) {
        errno = EFAULT;
        return -1;
    }
    const short allowed = static_cast<short> (ZLINK_POLLIN | ZLINK_POLLOUT);
    if (events_ == 0 || (events_ & ~allowed) != 0) {
        errno = EINVAL;
        return -1;
    }
    const zlink_fd_t fd = static_cast<zlink_fd_t> (node->ready_signaler.get_fd ());
    const int index = poller_find_fd_registration_index (poller, fd, poller_subject_mesh_node);
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    if (poller->poller.modify_fd (fd, events_) != 0)
        return -1;
    poller->registrations[static_cast<size_t> (index)].events = events_;
    return 0;
}

int zlink::mesh::poller_remove (void *poller_, void *handle_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    mesh_node_pin_t node_pin (handle_);
    mesh_node_t *node = node_pin.get ();
    if (!poller || !node) {
        errno = EFAULT;
        return -1;
    }
    const zlink_fd_t fd = static_cast<zlink_fd_t> (node->ready_signaler.get_fd ());
    const int index = poller_find_fd_registration_index (poller, fd, poller_subject_mesh_node);
    if (index >= 0)
        (void) poller_remove_registration_at (poller, index);
    std::lock_guard<std::mutex> lock (node->mutex);
    node->pollin_registered = false;
    if (node->pollin_signaled) {
        (void) node->ready_signaler.recv_failable ();
        node->pollin_signaled = false;
    }
    return 0;
}
