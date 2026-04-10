/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"

namespace zlink
{
int spot_node_t::apply_pub_defaults (spot_pub_t *pub_,
                                     const pub_defaults_t &defaults_)
{
    if (!pub_) {
        errno = EINVAL;
        return -1;
    }

    if (defaults_.sndhwm.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_SNDHWM, &defaults_.sndhwm.value,
                             defaults_.sndhwm.size)
             != 0)
        return -1;
    if (defaults_.sndtimeo.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                             &defaults_.sndtimeo.value, defaults_.sndtimeo.size)
             != 0)
        return -1;
    if (defaults_.linger.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_LINGER, &defaults_.linger.value,
                             defaults_.linger.size)
             != 0)
        return -1;
    if (defaults_.nodrop.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_NODROP, &defaults_.nodrop.value,
                             defaults_.nodrop.size)
             != 0)
        return -1;
    if (defaults_.sndbuf.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_SNDBUF, &defaults_.sndbuf.value,
                             defaults_.sndbuf.size)
             != 0)
        return -1;
    if (defaults_.rcvbuf.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_RCVBUF, &defaults_.rcvbuf.value,
                             defaults_.rcvbuf.size)
             != 0)
        return -1;
    return 0;
}

int spot_node_t::apply_sub_defaults (spot_sub_t *sub_,
                                     const sub_defaults_t &defaults_)
{
    if (!sub_) {
        errno = EINVAL;
        return -1;
    }

    if (defaults_.rcvhwm.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_RCVHWM, &defaults_.rcvhwm.value,
                             defaults_.rcvhwm.size)
             != 0)
        return -1;
    if (defaults_.linger.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_LINGER, &defaults_.linger.value,
                             defaults_.linger.size)
             != 0)
        return -1;
    if (defaults_.sndbuf.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_SNDBUF, &defaults_.sndbuf.value,
                             defaults_.sndbuf.size)
             != 0)
        return -1;
    if (defaults_.rcvbuf.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_RCVBUF, &defaults_.rcvbuf.value,
                             defaults_.rcvbuf.size)
             != 0)
        return -1;
    if (defaults_.rcvtimeo.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                             &defaults_.rcvtimeo.value,
                             defaults_.rcvtimeo.size)
             != 0)
        return -1;
    return 0;
}

spot_pub_t *spot_node_t::create_spot_pub_with_defaults (
  const pub_defaults_t &defaults_, bool node_owned_default_)
{
    if (ensure_healthy () != 0)
        return NULL;
    uint64_t attachment_id = 0;
    socket_base_t *attachment_socket = NULL;
    if (!_runtime
        || _runtime->create_attachment (spot_attachment_pub,
                                        pub_ingress_endpoint ().c_str (),
                                        &attachment_id)
             != 0)
        return NULL;
    attachment_socket = _runtime->attachment_socket (attachment_id);
    if (!attachment_socket || wait_facade_peer (attachment_socket) != 0) {
        const int err = errno != 0 ? errno : ETIMEDOUT;
        (void) _runtime->destroy_attachment (attachment_id);
        errno = err;
        return NULL;
    }

    spot_pub_t *pub = new (std::nothrow)
      spot_pub_t (this, attachment_socket, attachment_id, node_owned_default_);
    if (!pub) {
        (void) _runtime->destroy_attachment (attachment_id);
        errno = ENOMEM;
        return NULL;
    }

    if (apply_pub_defaults (pub, defaults_) != 0) {
        const int err = errno;
        pub->abort_create ();
        delete pub;
        errno = err;
        return NULL;
    }

    bool bound = false;
    spot_pub_t *published_default_pub = pub;
    {
        scoped_lock_t lock (_sync);
        _pubs.insert (pub);
        bound = !_bound_endpoint.empty ();
    }
    if (node_owned_default_)
        _handle_defaults.publish_default_pub (pub, &published_default_pub);

    if (node_owned_default_ && published_default_pub != pub) {
        remove_spot_pub (pub);
        pub->abort_create ();
        delete pub;
        return published_default_pub;
    }

    pub->emit_ready_event ();
    if (node_owned_default_) {
        zlink_send_ready_handler_fn handler =
          _send_ready_handler.load (std::memory_order_acquire);
        if (handler
            && pub->set_send_ready_handler (
                 handler, this,
                 _send_ready_handler_userdata.load (
                   std::memory_order_acquire))
                 != 0) {
            const int err = errno;
            remove_spot_pub (pub);
            pub->abort_create ();
            delete pub;
            errno = err;
            return NULL;
        }
    }
    if (bound)
        submit_pub_summary (pub, ZLINK_TOPOLOGY_STATE_READY, 0);
    return pub;
}

spot_sub_t *spot_node_t::create_spot_sub_with_defaults (
  const sub_defaults_t &defaults_, bool node_owned_default_)
{
    if (ensure_healthy () != 0)
        return NULL;
    uint64_t attachment_id = 0;
    socket_base_t *attachment_socket = NULL;
    if (!_runtime
        || _runtime->create_attachment (spot_attachment_sub,
                                        sub_fanout_endpoint ().c_str (),
                                        &attachment_id)
             != 0)
        return NULL;
    attachment_socket = _runtime->attachment_socket (attachment_id);
    if (!attachment_socket || wait_facade_peer (attachment_socket) != 0) {
        const int err = errno != 0 ? errno : ETIMEDOUT;
        (void) _runtime->destroy_attachment (attachment_id);
        errno = err;
        return NULL;
    }

    spot_sub_t *sub = new (std::nothrow)
      spot_sub_t (this, attachment_socket, attachment_id, node_owned_default_);
    if (!sub) {
        (void) _runtime->destroy_attachment (attachment_id);
        errno = ENOMEM;
        return NULL;
    }

    if (apply_sub_defaults (sub, defaults_) != 0) {
        const int err = errno;
        sub->abort_create ();
        delete sub;
        errno = err;
        return NULL;
    }

    {
        scoped_lock_t lock (_sync);
        _subs.insert (sub);
    }
    if (node_owned_default_)
        _handle_defaults.publish_default_sub (sub);
    sub->emit_ready_event ();
    submit_sub_summary (sub, ZLINK_TOPOLOGY_STATE_CONNECTING, 0);
    return sub;
}

spot_pub_t *spot_node_t::ensure_default_pub ()
{
    spot_pub_t *pub = _handle_defaults.fast_default_pub ();
    if (pub)
        return pub;

    scoped_lock_t init_lock (_handle_defaults.default_pub_init_lock ());
    pub = _handle_defaults.default_pub ();
    if (pub)
        return pub;

    pub_defaults_t defaults = _handle_defaults.load_pub_defaults ();
    pub = _handle_defaults.default_pub ();
    if (pub)
        return pub;
    return create_spot_pub_with_defaults (defaults, true);
}

spot_sub_t *spot_node_t::ensure_default_sub ()
{
    spot_sub_t *sub = _handle_defaults.fast_default_sub ();
    if (sub)
        return sub;

    scoped_lock_t init_lock (_handle_defaults.default_sub_init_lock ());
    sub = _handle_defaults.default_sub ();
    if (sub)
        return sub;

    sub_defaults_t defaults = _handle_defaults.load_sub_defaults ();
    sub = _handle_defaults.default_sub ();
    if (sub)
        return sub;
    return create_spot_sub_with_defaults (defaults, true);
}
}
