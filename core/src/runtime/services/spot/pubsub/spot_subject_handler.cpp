/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_subject_access.hpp"

#include "api/service/service_mode_internal.hpp"
#include "api/spot/subject/service_spot_subject_surface_internal.hpp"

#include <map>

#include "services/spot/dispatch/spot_dispatch_internal.hpp"
#include "services/spot/dispatch/spot_internal_receiver.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

namespace
{
zlink::spot_node_t *&spot_node_send_ready_callback_tls ()
{
    static thread_local zlink::spot_node_t *node = NULL;
    return node;
}

struct spot_node_handler_entry_t
{
    zlink_subscribe_handler_fn handler;
    void *userdata;
};

struct spot_node_handler_registry_t
{
    zlink::mutex_t sync;
    std::map<zlink::spot_node_t *, spot_node_handler_entry_t> handlers;
};

spot_node_handler_registry_t &spot_node_handler_registry ()
{
    static spot_node_handler_registry_t registry;
    return registry;
}

void spot_node_sub_handler_adapter (const zlink_routing_id_t *source_rid_,
                                    const char *topic_,
                                    size_t topic_len_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_)
{
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (userdata_);
    if (!node || !node->check_tag ()) {
        zlink_multipart_close (parts_, part_count_);
        return;
    }

    zlink_subscribe_handler_fn handler = NULL;
    void *userdata = NULL;
    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::spot_node_t *, spot_node_handler_entry_t>::iterator it =
          registry.handlers.find (node);
        if (it != registry.handlers.end ()) {
            handler = it->second.handler;
            userdata = it->second.userdata;
        }
    }

    if (!handler) {
        zlink_multipart_close (parts_, part_count_);
        return;
    }

    const zlink::spot_dispatch_context_t dispatch_context (node, true);
    handler (source_rid_, topic_, topic_len_, parts_, part_count_, userdata);
}

} // namespace

void spot_subject_composite_sub_handler_adapter (const zlink_routing_id_t *source_rid_,
                                                 const char *topic_,
                                                 size_t topic_len_,
                                                 zlink_msg_t *parts_,
                                                 size_t part_count_,
                                                 void *userdata_)
{
    spot_handle_t *spot = static_cast<spot_handle_t *> (userdata_);
    if (!spot || !spot->check_tag () || !spot->handler) {
        zlink_multipart_close (parts_, part_count_);
        return;
    }

    const zlink::spot_dispatch_context_t dispatch_context (spot, false);
    spot->handler (source_rid_, topic_, topic_len_, parts_, part_count_, spot->handler_userdata);
}

void spot_sub_dispatch_event_handler_adapter (const zlink_routing_id_t *source_rid_,
                                              const char *topic_,
                                              size_t topic_len_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_,
                                              void *userdata_)
{
    spot_handle_t *spot = static_cast<spot_handle_t *> (userdata_);
    if (!spot || !spot->check_tag ()) {
        zlink_multipart_close (parts_, part_count_);
        return;
    }

    if (spot_dispatch_queue_subscribe_message (spot, source_rid_, topic_, topic_len_, parts_,
                                               part_count_)
        != 0) {
        zlink_multipart_close (parts_, part_count_);
    }
}

int spot_pub_install_send_ready_handler (void *spot_pub_,
                                         zlink_send_ready_handler_fn handler_,
                                         void *userdata_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return -1;
    }
    if (pub->node ()) {
        zlink::service_public_api_scope_t admission (pub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return -1;
    }
    void *subject = spot_pub_;
    if (pub->is_node_owned_default () && pub->node ())
        subject = pub->node ();
    return pub->set_send_ready_handler (handler_, subject, userdata_);
}

bool in_spot_node_send_ready_callback (zlink::spot_node_t *node_)
{
    if (!node_)
        return false;

    if (spot_node_send_ready_callback_tls () == node_)
        return true;

    zlink::socket_base_t *dispatch_socket =
      zlink::socket_base_t::current_send_ready_dispatch_socket ();
    if (!dispatch_socket)
        return false;

    LIBZLINK_UNUSED (dispatch_socket);
    return false;
}

zlink::spot_node_t *enter_spot_node_send_ready_callback (zlink::spot_node_t *node_)
{
    zlink::spot_node_t *previous = spot_node_send_ready_callback_tls ();
    spot_node_send_ready_callback_tls () = node_;
    return previous;
}

void leave_spot_node_send_ready_callback (zlink::spot_node_t *previous_)
{
    spot_node_send_ready_callback_tls () = previous_;
}

void clear_spot_node_handler_registration (zlink::spot_node_t *node_)
{
    if (!node_)
        return;

    spot_node_handler_registry_t &registry = spot_node_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.handlers.erase (node_);
}

int spot_install_handler (spot_handle_t *spot_,
                          zlink_subscribe_handler_fn handler_,
                          void *userdata_)
{
    if (!spot_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    spot_->handler = handler_;
    spot_->handler_userdata = userdata_;
    return 0;
}

int spot_node_install_handler (zlink::spot_node_t *node_,
                               zlink_subscribe_handler_fn handler_,
                               void *userdata_)
{
    if (!node_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node_);
    if (!receiver)
        return -1;

    spot_node_handler_entry_t previous = {NULL, NULL};
    bool had_previous = false;
    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::spot_node_t *, spot_node_handler_entry_t>::iterator it =
          registry.handlers.find (node_);
        if (it != registry.handlers.end ()) {
            previous = it->second;
            had_previous = true;
        }
        spot_node_handler_entry_t entry = {handler_, userdata_};
        registry.handlers[node_] = entry;
    }

    if (receiver->set_direct_handler (&spot_node_sub_handler_adapter, node_) == 0)
        return 0;

    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        if (had_previous)
            registry.handlers[node_] = previous;
        else
            registry.handlers.erase (node_);
    }
    return -1;
}

int spot_install_recv_handler (spot_handle_t *spot_,
                               zlink_subscribe_handler_fn handler_,
                               void *userdata_)
{
    if (!spot_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    zlink::service_public_api_scope_t admission (spot_->public_api);
    if (!admission.acquired ())
        return -1;
    if (spot_transition_to_callback_mode (spot_) != 0)
        return -1;

    const int rc = spot_install_handler (spot_, handler_, userdata_);
    if (rc != 0)
        spot_revert_callback_transition (spot_);
    return rc;
}

int spot_install_dispatch_event_sub_handler (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int spot_node_install_recv_handler (zlink::spot_node_t *node_,
                                    zlink_subscribe_handler_fn handler_,
                                    void *userdata_)
{
    if (!node_ || !handler_) {
        errno = EINVAL;
        return -1;
    }
    if (!node_->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (spot_node_transition_to_callback_mode (node_) != 0)
        return -1;

    const int rc = spot_node_install_handler (node_, handler_, userdata_);
    if (rc != 0)
        spot_node_revert_callback_transition (node_);
    return rc;
}

int spot_install_send_ready_handler (spot_handle_t *spot_,
                                     zlink_send_ready_handler_fn handler_,
                                     void *userdata_)
{
    if (!spot_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    zlink::service_public_api_scope_t admission (spot_->public_api);
    if (!admission.acquired ())
        return -1;
    if (in_spot_node_send_ready_callback (spot_->node)) {
        errno = EDEADLK;
        return -1;
    }
    bool already_active = false;
    if (spot_activate_send_ready_mode (spot_, &already_active) != 0)
        return -1;
    if (!spot_->logical_state || !spot_->node) {
        spot_revert_send_ready_mode (spot_);
        errno = EFAULT;
        return -1;
    }
    spot_->logical_state->send_ready_userdata.store (userdata_, std::memory_order_release);
    spot_->logical_state->send_ready_subject.store (spot_, std::memory_order_release);
    spot_->logical_state->send_ready_handler.store (handler_, std::memory_order_release);
    return 0;
}

int spot_node_install_send_ready_handler (zlink::spot_node_t *node_,
                                          zlink_send_ready_handler_fn handler_,
                                          void *userdata_)
{
    if (!node_ || !handler_) {
        errno = EINVAL;
        return -1;
    }
    if (!node_->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (in_spot_node_send_ready_callback (node_)) {
        errno = EDEADLK;
        return -1;
    }
    bool already_active = false;
    if (spot_node_activate_send_ready_mode (node_, &already_active) != 0)
        return -1;
    const int rc = node_->set_send_ready_handler (handler_, userdata_);
    if (rc != 0 && !already_active)
        spot_node_revert_send_ready_mode (node_);
    return rc;
}
