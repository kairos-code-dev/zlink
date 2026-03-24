/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_subject_access.hpp"

#include "api/service_api_internal.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <string.h>
#include <vector>

#include "services/spot/spot_dispatch_internal.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

namespace
{
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

thread_local void *g_current_spot_dispatch_handle = NULL;
thread_local bool g_current_spot_dispatch_is_node = false;

spot_node_handler_registry_t &spot_node_handler_registry ()
{
    static spot_node_handler_registry_t registry;
    return registry;
}

void close_spot_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
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
        close_spot_parts (parts_, part_count_);
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
        close_spot_parts (parts_, part_count_);
        return;
    }

    g_current_spot_dispatch_handle = node;
    g_current_spot_dispatch_is_node = true;
    handler (source_rid_, topic_, topic_len_, parts_, part_count_, userdata);
    g_current_spot_dispatch_handle = NULL;
    g_current_spot_dispatch_is_node = false;
}

void spot_sub_handler_adapter (const zlink_routing_id_t *source_rid_,
                               const char *topic_,
                               size_t topic_len_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               void *userdata_)
{
    spot_handle_t *spot = static_cast<spot_handle_t *> (userdata_);
    if (!spot || !spot->check_tag () || !spot->handler) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    g_current_spot_dispatch_handle = spot;
    g_current_spot_dispatch_is_node = false;
    spot->handler (source_rid_, topic_, topic_len_, parts_, part_count_,
                   spot->handler_userdata);
    g_current_spot_dispatch_handle = NULL;
    g_current_spot_dispatch_is_node = false;
}
} // namespace

namespace zlink
{
void *current_spot_dispatch_handle ()
{
    return g_current_spot_dispatch_handle;
}

bool current_spot_dispatch_is_node ()
{
    return g_current_spot_dispatch_is_node;
}
}

zlink::spot_pub_t *as_spot_pub_side_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (handle_);
    return pub->check_tag () ? pub : NULL;
}

zlink::spot_sub_t *as_spot_sub_side_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (handle_);
    return sub->check_tag () ? sub : NULL;
}

zlink::spot_node_t *as_spot_node_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
    return node->check_tag () ? node : NULL;
}

spot_handle_t *as_spot_handle (void *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return NULL;
    }

    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return spot;
}

zlink::spot_pub_t *ensure_spot_pub (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->node) {
        errno = EFAULT;
        return NULL;
    }

    if (spot_->pub)
        return spot_->pub;

    spot_->pub = spot_->node->create_spot_pub ();
    return spot_->pub;
}

zlink::spot_sub_t *ensure_spot_sub (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->node) {
        errno = EFAULT;
        return NULL;
    }

    if (spot_->sub)
        return spot_->sub;

    zlink::spot_sub_t *sub = spot_->node->create_spot_sub ();
    if (!sub)
        return NULL;

    if (spot_->handler
        && sub->set_direct_handler (&spot_sub_handler_adapter, spot_) != 0) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    const zlink::spot_node_t::sub_defaults_t &defaults =
      spot_->pending_sub_defaults;
    if ((defaults.rcvhwm.enabled
         && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVHWM, &defaults.rcvhwm.value,
                             defaults.rcvhwm.size)
              != 0)
        || (defaults.linger.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_LINGER,
                                &defaults.linger.value,
                                defaults.linger.size)
                 != 0)
        || (defaults.sndbuf.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_SNDBUF,
                                &defaults.sndbuf.value,
                                defaults.sndbuf.size)
                 != 0)
        || (defaults.rcvbuf.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVBUF,
                                &defaults.rcvbuf.value,
                                defaults.rcvbuf.size)
                 != 0)
        || (defaults.rcvtimeo.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                                &defaults.rcvtimeo.value,
                                defaults.rcvtimeo.size)
                 != 0)) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    spot_->sub = sub;
    return spot_->sub;
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
        zlink::service_public_api_scope_t admission (
          pub->node ()->public_api_guard ());
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

    zlink::socket_base_t *dispatch_socket =
      zlink::socket_base_t::current_send_ready_dispatch_socket ();
    if (!dispatch_socket)
        return false;

    zlink::spot_pub_t *pub = node_->default_pub ();
    return pub && pub->owns_socket (dispatch_socket);
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

    zlink::spot_sub_t *sub = ensure_spot_sub (spot_);
    if (!sub)
        return -1;

    spot_->handler = handler_;
    spot_->handler_userdata = userdata_;
    const int rc = sub->set_direct_handler (&spot_sub_handler_adapter, spot_);
    if (rc != 0) {
        spot_->handler = NULL;
        spot_->handler_userdata = NULL;
    }
    return rc;
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

    if (receiver->set_direct_handler (&spot_node_sub_handler_adapter, node_)
        == 0)
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
    bool already_active = false;
    if (spot_activate_send_ready_mode (spot_, &already_active) != 0)
        return -1;
    zlink::spot_pub_t *pub = ensure_spot_pub (spot_);
    if (!pub) {
        if (!already_active)
            spot_revert_send_ready_mode (spot_);
        errno = ENOTSUP;
        return -1;
    }
    const int rc = pub->set_send_ready_handler (handler_, spot_, userdata_);
    if (rc != 0 && !already_active)
        spot_revert_send_ready_mode (spot_);
    return rc;
}

int spot_node_install_send_ready_handler (
  zlink::spot_node_t *node_,
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
    bool already_active = false;
    if (spot_node_activate_send_ready_mode (node_, &already_active) != 0)
        return -1;
    const int rc = node_->set_send_ready_handler (handler_, userdata_);
    if (rc != 0 && !already_active)
        spot_node_revert_send_ready_mode (node_);
    return rc;
}
