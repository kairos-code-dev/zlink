/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"

namespace zlink
{
namespace
{
static void preserve_first_error_local (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}
}

spot_node_default_handles_t::spot_node_default_handles_t () :
    _default_pub (NULL),
    _default_sub (NULL),
    _internal_receiver (NULL),
    _default_pub_fast (NULL),
    _default_sub_fast (NULL),
    _internal_receiver_fast (NULL)
{
}

int spot_node_default_handles_t::validate_pub_option (int option_,
                                                      const void *optval_,
                                                      size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0 || optvallen_ > sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
        case ZLINK_SPOT_PUB_OPT_LINGER:
        case ZLINK_SPOT_PUB_OPT_NODROP:
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int spot_node_default_handles_t::validate_sub_option (int option_,
                                                      const void *optval_,
                                                      size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0 || optvallen_ > sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
        case ZLINK_SPOT_SUB_OPT_LINGER:
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

void spot_node_default_handles_t::copy_option_setting (option_setting_t *dst_,
                                                       const void *optval_,
                                                       size_t optvallen_)
{
    if (!dst_)
        return;

    dst_->enabled = true;
    dst_->value = 0;
    dst_->size = optvallen_;
    memcpy (&dst_->value, optval_, optvallen_);
}

void spot_node_default_handles_t::store_pub_option (int option_,
                                                    const void *optval_,
                                                    size_t optvallen_)
{
    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
            copy_option_setting (&_pub_defaults.sndhwm, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
            copy_option_setting (&_pub_defaults.sndtimeo, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_LINGER:
            copy_option_setting (&_pub_defaults.linger, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_NODROP:
            copy_option_setting (&_pub_defaults.nodrop, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
            copy_option_setting (&_pub_defaults.sndbuf, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
            copy_option_setting (&_pub_defaults.rcvbuf, optval_, optvallen_);
            return;
        default:
            return;
    }
}

void spot_node_default_handles_t::store_sub_option (int option_,
                                                    const void *optval_,
                                                    size_t optvallen_)
{
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            copy_option_setting (&_sub_defaults.rcvhwm, optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_LINGER:
            copy_option_setting (&_sub_defaults.linger, optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            copy_option_setting (&_sub_defaults.sndbuf, optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            copy_option_setting (&_sub_defaults.rcvbuf, optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            copy_option_setting (&_sub_defaults.rcvtimeo, optval_, optvallen_);
            return;
        default:
            return;
    }
}

int spot_node_default_handles_t::set_pub_option (int option_,
                                                 const void *optval_,
                                                 size_t optvallen_)
{
    if (validate_pub_option (option_, optval_, optvallen_) != 0)
        return -1;

    scoped_lock_t init_lock (_default_pub_sync);
    spot_pub_t *default_pub = NULL;
    {
        scoped_lock_t lock (_sync);
        default_pub = _default_pub;
    }

    if (default_pub && default_pub->set_option (option_, optval_, optvallen_) != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        store_pub_option (option_, optval_, optvallen_);
    }
    return 0;
}

int spot_node_default_handles_t::set_sub_option (int option_,
                                                 const void *optval_,
                                                 size_t optvallen_)
{
    if (validate_sub_option (option_, optval_, optvallen_) != 0)
        return -1;

    scoped_lock_t init_lock (_default_sub_sync);
    spot_sub_t *default_sub = NULL;
    spot_internal_receiver_t *internal_receiver = NULL;
    {
        scoped_lock_t lock (_sync);
        default_sub = _default_sub;
        internal_receiver = _internal_receiver;
    }

    if (default_sub && default_sub->set_option (option_, optval_, optvallen_) != 0)
        return -1;
    if (internal_receiver
        && internal_receiver->impl () != default_sub
        && internal_receiver->set_option (option_, optval_, optvallen_) != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        store_sub_option (option_, optval_, optvallen_);
    }
    return 0;
}

spot_node_default_handles_t::pub_defaults_t
spot_node_default_handles_t::load_pub_defaults () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _pub_defaults;
}

spot_node_default_handles_t::sub_defaults_t
spot_node_default_handles_t::load_sub_defaults () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _sub_defaults;
}

spot_pub_t *spot_node_default_handles_t::default_pub () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _default_pub;
}

spot_sub_t *spot_node_default_handles_t::default_sub () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _default_sub;
}

spot_internal_receiver_t *spot_node_default_handles_t::internal_receiver () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _internal_receiver;
}

spot_pub_t *spot_node_default_handles_t::fast_default_pub () const
{
    return _default_pub_fast.load (std::memory_order_acquire);
}

spot_sub_t *spot_node_default_handles_t::fast_default_sub () const
{
    return _default_sub_fast.load (std::memory_order_acquire);
}

spot_internal_receiver_t *
spot_node_default_handles_t::fast_internal_receiver () const
{
    return _internal_receiver_fast.load (std::memory_order_acquire);
}

void spot_node_default_handles_t::publish_default_pub (
  spot_pub_t *pub_,
  spot_pub_t **published_default_pub_out_)
{
    if (published_default_pub_out_)
        *published_default_pub_out_ = pub_;

    scoped_lock_t lock (_sync);
    if (_default_pub && _default_pub != pub_) {
        if (published_default_pub_out_)
            *published_default_pub_out_ = _default_pub;
        return;
    }

    _default_pub = pub_;
    _default_pub_fast.store (pub_, std::memory_order_release);
}

void spot_node_default_handles_t::publish_default_sub (spot_sub_t *sub_)
{
    scoped_lock_t lock (_sync);
    _default_sub = sub_;
    _default_sub_fast.store (sub_, std::memory_order_release);
}

spot_internal_receiver_t *spot_node_default_handles_t::publish_internal_receiver (
  spot_internal_receiver_t *receiver_,
  spot_sub_t *created_sub_,
  spot_sub_t *previous_default_sub_,
  bool *installed_out_)
{
    if (installed_out_)
        *installed_out_ = false;

    scoped_lock_t lock (_sync);
    if (_default_sub == created_sub_)
        _default_sub = previous_default_sub_;

    if (_internal_receiver && _internal_receiver != receiver_) {
        _default_sub_fast.store (_default_sub, std::memory_order_release);
        return _internal_receiver;
    }

    _internal_receiver = receiver_;
    _internal_receiver_fast.store (receiver_, std::memory_order_release);
    _default_sub_fast.store (_default_sub, std::memory_order_release);
    if (installed_out_)
        *installed_out_ = true;
    return receiver_;
}

void spot_node_default_handles_t::remove_spot_pub (spot_pub_t *pub_)
{
    scoped_lock_t lock (_sync);
    if (_default_pub == pub_) {
        _default_pub = NULL;
        _default_pub_fast.store (NULL, std::memory_order_release);
    }
}

bool spot_node_default_handles_t::remove_spot_sub (spot_sub_t *sub_)
{
    bool had_filters = false;
    scoped_lock_t lock (_sync);
    if (_default_sub == sub_) {
        _default_sub = NULL;
        _default_sub_fast.store (NULL, std::memory_order_release);
    }
    if (_internal_receiver && _internal_receiver->impl () == sub_) {
        _internal_receiver = NULL;
        _internal_receiver_fast.store (NULL, std::memory_order_release);
    }
    had_filters = sub_ && sub_->has_filters ();
    return had_filters;
}

void spot_node_default_handles_t::snapshot_destroy_handles (
  const std::set<spot_pub_t *> &pubs_,
  const std::set<spot_sub_t *> &subs_,
  std::vector<spot_pub_t *> *pubs_out_,
  std::vector<spot_sub_t *> *subs_out_)
{
    if (!pubs_out_ || !subs_out_)
        return;

    pubs_out_->clear ();
    subs_out_->clear ();

    scoped_lock_t lock (_sync);
    pubs_out_->assign (pubs_.begin (), pubs_.end ());
    for (std::set<spot_sub_t *>::const_iterator it = subs_.begin ();
         it != subs_.end (); ++it) {
        if (_internal_receiver && *it == _internal_receiver->impl ())
            continue;
        subs_out_->push_back (*it);
    }

    _default_pub = NULL;
    _default_sub = NULL;
    _default_pub_fast.store (NULL, std::memory_order_release);
    _default_sub_fast.store (NULL, std::memory_order_release);
    _internal_receiver_fast.store (NULL, std::memory_order_release);
}

spot_internal_receiver_t *spot_node_default_handles_t::detach_internal_receiver ()
{
    scoped_lock_t lock (_sync);
    spot_internal_receiver_t *receiver = _internal_receiver;
    _internal_receiver = NULL;
    _internal_receiver_fast.store (NULL, std::memory_order_release);
    return receiver;
}

int spot_node_t::set_pub_option (int option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    return _handle_defaults.set_pub_option (option_, optval_, optvallen_);
}

int spot_node_t::set_sub_option (int option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    return _handle_defaults.set_sub_option (option_, optval_, optvallen_);
}

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

spot_pub_t *spot_node_t::create_spot_pub ()
{
    pub_defaults_t defaults;
    memset (&defaults, 0, sizeof (defaults));
    return create_spot_pub_with_defaults (defaults, false);
}

spot_sub_t *spot_node_t::create_spot_sub ()
{
    sub_defaults_t defaults;
    memset (&defaults, 0, sizeof (defaults));
    return create_spot_sub_with_defaults (defaults, false);
}

spot_internal_receiver_t *spot_node_t::ensure_internal_receiver ()
{
    spot_internal_receiver_t *receiver = _handle_defaults.fast_internal_receiver ();
    if (receiver)
        return receiver;

    scoped_lock_t init_lock (_handle_defaults.default_sub_init_lock ());

    spot_sub_t *previous_default_sub = NULL;
    receiver = _handle_defaults.internal_receiver ();
    if (receiver)
        return receiver;
    previous_default_sub = _handle_defaults.default_sub ();

    sub_defaults_t defaults = _handle_defaults.load_sub_defaults ();
    receiver = _handle_defaults.internal_receiver ();
    if (receiver)
        return receiver;

    spot_sub_t *sub = create_spot_sub_with_defaults (defaults, true);
    if (!sub)
        return NULL;
    receiver = new (std::nothrow) spot_internal_receiver_t (sub);
    if (!receiver) {
        (void) sub->abort_create ();
        delete sub;
        errno = ENOMEM;
        return NULL;
    }

    bool installed = false;
    spot_internal_receiver_t *published_receiver =
      _handle_defaults.publish_internal_receiver (receiver, sub,
                                                 previous_default_sub,
                                                 &installed);
    if (!installed) {
        {
            scoped_lock_t lock (_sync);
            _subs.erase (sub);
        }
        (void) receiver->abort_create ();
        delete receiver;
        delete sub;
        return published_receiver;
    }

    return receiver;
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

spot_pub_t *spot_node_t::default_pub () const
{
    return _handle_defaults.default_pub ();
}

spot_internal_receiver_t *spot_node_t::internal_receiver () const
{
    return _handle_defaults.internal_receiver ();
}

spot_sub_t *spot_node_t::default_sub () const
{
    return _handle_defaults.default_sub ();
}

void spot_node_t::remove_spot_pub (spot_pub_t *pub_)
{
    _handle_defaults.remove_spot_pub (pub_);
    scoped_lock_t lock (_sync);
    _pubs.erase (pub_);
}

void spot_node_t::remove_spot_sub (spot_sub_t *sub_)
{
    const bool had_filters = _handle_defaults.remove_spot_sub (sub_);
    scoped_lock_t lock (_sync);
    _subs.erase (sub_);
    if (had_filters)
        note_local_sub_filters_changed (true, false);
}

int spot_node_t::destroy_handles ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    int first_error = 0;
    {
        scoped_lock_t lock (_sync);
        _handle_defaults.snapshot_destroy_handles (_pubs, _subs, &pubs, &subs);
        for (size_t i = 0; i < pubs.size (); ++i)
            _pubs.erase (pubs[i]);
        for (size_t i = 0; i < subs.size (); ++i)
            _subs.erase (subs[i]);
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        preserve_first_error_local (pubs[i]->destroy_from_node (), &first_error);
        delete pubs[i];
    }
    for (size_t i = 0; i < subs.size (); ++i) {
        preserve_first_error_local (subs[i]->destroy_from_node (), &first_error);
        delete subs[i];
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

int spot_node_t::destroy_internal_receiver ()
{
    spot_internal_receiver_t *receiver = _handle_defaults.detach_internal_receiver ();
    {
        scoped_lock_t lock (_sync);
        if (receiver)
            _subs.erase (receiver->impl ());
    }

    if (!receiver)
        return 0;

    const bool had_filters = receiver->has_filters ();
    const int rc = receiver->abort_create ();
    spot_sub_t *sub = receiver->impl ();
    delete receiver;
    delete sub;
    if (had_filters)
        note_local_sub_filters_changed (true, false);
    return rc;
}

void spot_node_t::stop_data_plane_sockets ()
{
    if (_runtime)
        _runtime->stop_sockets ();
}

void spot_node_t::close_control_sockets ()
{
    if (_runtime)
        _runtime->close_control_sockets ();
}
}
