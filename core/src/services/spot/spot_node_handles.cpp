/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"
#include "core/auto_hwm_policy.hpp"

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

static void copy_int_option_value (const void *optval_,
                                   size_t optvallen_,
                                   int *out_)
{
    if (!optval_ || !out_ || optvallen_ == 0)
        return;

    *out_ = 0;
    memcpy (out_, optval_, std::min (optvallen_, sizeof (*out_)));
}

static void refresh_runtime_pub_hwm (spot_runtime_t *runtime_,
                                     const void *optval_,
                                     size_t optvallen_)
{
    if (!runtime_ || !optval_ || optvallen_ == 0)
        return;

    int value = 0;
    copy_int_option_value (optval_, optvallen_, &value);
    if (runtime_->mesh_pub)
        (void) runtime_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                               &value, sizeof (value));
    if (runtime_->local_fanout_xpub)
        (void) runtime_->local_fanout_xpub->setsockopt (
          ZLINK_INTERNAL_OPT_SNDHWM, &value, sizeof (value));
    runtime_->execution.data_plane_state.mesh_pub_budget.current_sndhwm = value;
}

static void refresh_runtime_sub_hwm (spot_runtime_t *runtime_,
                                     const void *optval_,
                                     size_t optvallen_)
{
    if (!runtime_ || !optval_ || optvallen_ == 0)
        return;

    int value = 0;
    copy_int_option_value (optval_, optvallen_, &value);
    if (runtime_->local_pub_ingress_sub)
        (void) runtime_->local_pub_ingress_sub->setsockopt (
          ZLINK_INTERNAL_OPT_RCVHWM, &value, sizeof (value));
    if (runtime_->mesh_xsub)
        (void) runtime_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                                &value, sizeof (value));
}

static void refresh_runtime_routed_send_hwm (spot_runtime_t *runtime_,
                                             const void *optval_,
                                             size_t optvallen_)
{
    if (!runtime_ || !optval_ || optvallen_ == 0)
        return;

    int value = 0;
    copy_int_option_value (optval_, optvallen_, &value);
    if (runtime_->node_router)
        (void) runtime_->node_router->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                                  &value, sizeof (value));
}

static void refresh_runtime_routed_recv_hwm (spot_runtime_t *runtime_,
                                             const void *optval_,
                                             size_t optvallen_)
{
    if (!runtime_ || !optval_ || optvallen_ == 0)
        return;

    int value = 0;
    copy_int_option_value (optval_, optvallen_, &value);
    if (runtime_->route_ingress)
        (void) runtime_->route_ingress->setsockopt (
          ZLINK_INTERNAL_OPT_RCVHWM, &value, sizeof (value));
    if (runtime_->node_router)
        (void) runtime_->node_router->setsockopt (
          ZLINK_INTERNAL_OPT_RCVHWM, &value, sizeof (value));
}

static void refresh_runtime_auto_hwm_msg_unit (spot_runtime_t *runtime_,
                                               const void *optval_,
                                               size_t optvallen_)
{
    if (!runtime_ || !optval_ || optvallen_ == 0)
        return;

    socket_base_t *sockets[] = {
      runtime_->mesh_pub,
      runtime_->local_fanout_xpub,
      runtime_->local_pub_ingress_sub,
      runtime_->mesh_xsub,
      runtime_->node_router,
      runtime_->route_ingress};

    for (size_t i = 0; i != sizeof (sockets) / sizeof (sockets[0]); ++i) {
        socket_base_t *socket = sockets[i];
        if (!socket)
            continue;
        (void) socket->setsockopt (ZLINK_INTERNAL_OPT_AUTO_HWM_MSG_UNIT_BYTES,
                                   optval_, optvallen_);
        socket->clear_auto_hwm_manual_overrides (true, true, true, true);
        socket->refresh_auto_hwm_policy (true);
    }

}

static bool apply_runtime_hwm_option (spot_node_hwm_config_t *config_,
                                      int option_,
                                      const void *optval_,
                                      size_t optvallen_)
{
    if (!config_ || !optval_ || optvallen_ == 0 || optvallen_ > sizeof (int))
        return false;

    int value = 0;
    copy_int_option_value (optval_, optvallen_, &value);
    switch (option_) {
        case ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM:
            config_->topic_send_enabled = true;
            config_->topic_send_hwm = value;
            return true;
        case ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM:
            config_->topic_recv_enabled = true;
            config_->topic_recv_hwm = value;
            return true;
        case ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM:
            config_->routed_send_enabled = true;
            config_->routed_send_hwm = value;
            return true;
        case ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM:
            config_->routed_recv_enabled = true;
            config_->routed_recv_hwm = value;
            return true;
        default:
            return false;
    }
}

static int compute_default_node_hwm (ctx_t *ctx_,
                                     const spot_runtime_t *runtime_,
                                     int option_)
{
    if (!ctx_)
        return 0;

    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_from_budget_mb (
      ctx_->get (ZLINK_CTX_OPT_AUTO_HWM_ENABLE) != 0,
      ctx_->get (ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB), &context_plan);

    auto_hwm_role_t role = auto_hwm_role_none;
    int socket_type = ZLINK_CORE_SOCKET_PAIR;
    size_t managed_connections = 0;
    size_t active_connections = 0;
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    if (runtime_) {
        runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                                            &connected_peer_count,
                                            &active_peer_count);
    }
    switch (option_) {
        case ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM:
            role = auto_hwm_role_fanout;
            socket_type = ZLINK_CORE_SOCKET_PUB;
            managed_connections = local_sub_count + connected_peer_count;
            active_connections = local_sub_count + active_peer_count;
            break;
        case ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM:
            role = auto_hwm_role_recv_ingress;
            socket_type = ZLINK_CORE_SOCKET_SUB;
            managed_connections = local_pub_count + connected_peer_count;
            active_connections = local_pub_count + active_peer_count;
            break;
        case ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM:
        case ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM:
            role = auto_hwm_role_routed;
            socket_type = ZLINK_CORE_SOCKET_ROUTER;
            managed_connections = connected_peer_count;
            active_connections = active_peer_count;
            break;
        default:
            return 0;
    }

    auto_hwm_socket_plan_t socket_plan;
    const uint32_t planning_bootstrap =
      static_cast<uint32_t> (ctx_->auto_hwm_spot_bootstrap ());
    auto_hwm_socket_plan_for_role (context_plan, role, socket_type,
                                   managed_connections, active_connections,
                                   &socket_plan, 0, -1, -1, false, false,
                                   auto_hwm_scope_none, 1, true,
                                   planning_bootstrap);
    return option_ == ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM
             || option_ == ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM
             ? socket_plan.rcvhwm
             : socket_plan.sndhwm;
}

static bool read_runtime_hwm_option (ctx_t *ctx_,
                                     const spot_runtime_t *runtime_,
                                     const spot_node_hwm_config_t &config_,
                                     int option_,
                                     int *value_out_)
{
    if (!value_out_)
        return false;

    switch (option_) {
        case ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM:
            if (config_.topic_send_enabled) {
                *value_out_ = config_.topic_send_hwm;
                return true;
            }
            *value_out_ = compute_default_node_hwm (ctx_, runtime_, option_);
            return true;
        case ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM:
            if (config_.topic_recv_enabled) {
                *value_out_ = config_.topic_recv_hwm;
                return true;
            }
            *value_out_ = compute_default_node_hwm (ctx_, runtime_, option_);
            return true;
        case ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM:
            if (config_.routed_send_enabled) {
                *value_out_ = config_.routed_send_hwm;
                return true;
            }
            *value_out_ = compute_default_node_hwm (ctx_, runtime_, option_);
            return true;
        case ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM:
            if (config_.routed_recv_enabled) {
                *value_out_ = config_.routed_recv_hwm;
                return true;
            }
            *value_out_ = compute_default_node_hwm (ctx_, runtime_, option_);
            return true;
        default:
            return false;
    }
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
        case ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
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
        case ZLINK_SPOT_SUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
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
        case ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
            copy_option_setting (&_pub_defaults.auto_hwm_msg_unit_bytes,
                                 optval_, optvallen_);
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
        case ZLINK_SPOT_SUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
            copy_option_setting (&_sub_defaults.auto_hwm_msg_unit_bytes,
                                 optval_, optvallen_);
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
    const int rc = _handle_state.handle_defaults.set_pub_option (option_, optval_, optvallen_);
    if (rc != 0)
        return rc;

    if (option_ == ZLINK_SPOT_PUB_OPT_SNDHWM)
        refresh_runtime_pub_hwm (_runtime, optval_, optvallen_);
    else if (option_ == ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES)
        refresh_runtime_auto_hwm_msg_unit (_runtime, optval_, optvallen_);
    return 0;
}

int spot_node_t::set_sub_option (int option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    const int rc = _handle_state.handle_defaults.set_sub_option (option_, optval_, optvallen_);
    if (rc != 0)
        return rc;

    if (option_ == ZLINK_SPOT_SUB_OPT_RCVHWM)
        refresh_runtime_sub_hwm (_runtime, optval_, optvallen_);
    else if (option_ == ZLINK_SPOT_SUB_OPT_AUTO_HWM_MSG_UNIT_BYTES)
        refresh_runtime_auto_hwm_msg_unit (_runtime, optval_, optvallen_);
    return 0;
}

int spot_node_t::set_node_option (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!_runtime || !optval_) {
        errno = EFAULT;
        return -1;
    }

    spot_node_hwm_config_t hwm_config = _runtime->hwm_config_snapshot ();
    if (!apply_runtime_hwm_option (&hwm_config, option_, optval_, optvallen_)) {
        errno = EINVAL;
        return -1;
    }
    if (option_ == ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM) {
        if (_handle_state.handle_defaults.set_pub_option (ZLINK_SPOT_PUB_OPT_SNDHWM, optval_,
                                             optvallen_)
            != 0)
            return -1;
    } else if (option_ == ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM) {
        if (_handle_state.handle_defaults.set_sub_option (ZLINK_SPOT_SUB_OPT_RCVHWM, optval_,
                                             optvallen_)
            != 0)
            return -1;
    }
    _runtime->set_hwm_config (hwm_config);

    switch (option_) {
        case ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM:
            refresh_runtime_pub_hwm (_runtime, optval_, optvallen_);
            return 0;
        case ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM:
            refresh_runtime_sub_hwm (_runtime, optval_, optvallen_);
            return 0;
        case ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM:
            refresh_runtime_routed_send_hwm (_runtime, optval_, optvallen_);
            return 0;
        case ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM:
            refresh_runtime_routed_recv_hwm (_runtime, optval_, optvallen_);
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int spot_node_t::get_node_option (int option_,
                                  void *optval_,
                                  size_t *optvallen_) const
{
    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;
    if (!_runtime || !optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int value = 0;
    const spot_node_hwm_config_t hwm_config = _runtime->hwm_config_snapshot ();
    if (!read_runtime_hwm_option (_ctx, _runtime, hwm_config, option_, &value)) {
        errno = EINVAL;
        return -1;
    }

    memcpy (optval_, &value, sizeof (value));
    *optvallen_ = sizeof (value);
    return 0;
}

spot_node_t::pub_defaults_t spot_node_t::load_pub_defaults () const
{
    return _handle_state.handle_defaults.load_pub_defaults ();
}

spot_node_t::sub_defaults_t spot_node_t::load_sub_defaults () const
{
    return _handle_state.handle_defaults.load_sub_defaults ();
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
    spot_internal_receiver_t *receiver = _handle_state.handle_defaults.fast_internal_receiver ();
    if (receiver)
        return receiver;

    scoped_lock_t init_lock (_handle_state.handle_defaults.default_sub_init_lock ());

    spot_sub_t *previous_default_sub = NULL;
    receiver = _handle_state.handle_defaults.internal_receiver ();
    if (receiver)
        return receiver;
    previous_default_sub = _handle_state.handle_defaults.default_sub ();

    sub_defaults_t defaults = _handle_state.handle_defaults.load_sub_defaults ();
    receiver = _handle_state.handle_defaults.internal_receiver ();
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
      _handle_state.handle_defaults.publish_internal_receiver (receiver, sub,
                                                 previous_default_sub,
                                                 &installed);
    if (!installed) {
        {
            scoped_lock_t lock (_sync);
            _handle_state.subs.erase (sub);
        }
        (void) receiver->abort_create ();
        delete receiver;
        delete sub;
        return published_receiver;
    }

    return receiver;
}

spot_pub_t *spot_node_t::default_pub () const
{
    return _handle_state.handle_defaults.default_pub ();
}

spot_internal_receiver_t *spot_node_t::internal_receiver () const
{
    return _handle_state.handle_defaults.internal_receiver ();
}

spot_sub_t *spot_node_t::default_sub () const
{
    return _handle_state.handle_defaults.default_sub ();
}

void spot_node_t::remove_spot_pub (spot_pub_t *pub_)
{
    _handle_state.handle_defaults.remove_spot_pub (pub_);
    scoped_lock_t lock (_sync);
    _handle_state.pubs.erase (pub_);
}

void spot_node_t::remove_spot_sub (spot_sub_t *sub_)
{
    const bool had_filters = _handle_state.handle_defaults.remove_spot_sub (sub_);
    scoped_lock_t lock (_sync);
    _handle_state.subs.erase (sub_);
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
        _handle_state.handle_defaults.snapshot_destroy_handles (_handle_state.pubs, _handle_state.subs, &pubs, &subs);
        for (size_t i = 0; i < pubs.size (); ++i)
            _handle_state.pubs.erase (pubs[i]);
        for (size_t i = 0; i < subs.size (); ++i)
            _handle_state.subs.erase (subs[i]);
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
    spot_internal_receiver_t *receiver = _handle_state.handle_defaults.detach_internal_receiver ();
    {
        scoped_lock_t lock (_sync);
        if (receiver)
            _handle_state.subs.erase (receiver->impl ());
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
