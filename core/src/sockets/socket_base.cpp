/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <algorithm>
#include <new>

#include "sockets/socket_base.hpp"
#include "core/ctx.hpp"
#include "core/mailbox.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include "sockets/pair.hpp"
#include "sockets/pub.hpp"
#include "sockets/sub.hpp"
#include "sockets/dealer.hpp"
#include "sockets/router.hpp"
#include "sockets/stream.hpp"
#include "sockets/xpub.hpp"
#include "sockets/xsub.hpp"
#include "services/discovery/socket_discovery_attachment.hpp"

namespace
{
const char peer_weight_cmd_name[] = "WEIGHT";
const size_t peer_weight_cmd_name_size = sizeof (peer_weight_cmd_name) - 1;

static void generate_default_routing_id (unsigned char out_[16])
{
    zlink::generate_random_bytes (out_, 16);

    // RFC 4122 variant/version layout.
    out_[6] = static_cast<unsigned char> ((out_[6] & 0x0F) | 0x40);
    out_[8] = static_cast<unsigned char> ((out_[8] & 0x3F) | 0x80);

    bool all_zero = true;
    for (size_t i = 0; i < 16; ++i) {
        if (out_[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero)
        out_[15] = 1;
}
}

bool zlink::socket_base_t::check_tag () const
{
    return _tag == 0xbaddecaf;
}

zlink::socket_base_t *zlink::socket_base_t::create (int type_,
                                                class ctx_t *parent_,
                                                uint32_t tid_,
                                                int sid_)
{
    socket_base_t *s = NULL;
    switch (type_) {
        case ZLINK_CORE_SOCKET_PAIR:
            s = new (std::nothrow) pair_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_PUB:
            s = new (std::nothrow) pub_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_SUB:
            s = new (std::nothrow) sub_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_DEALER:
            s = new (std::nothrow) dealer_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_ROUTER:
            s = new (std::nothrow) router_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_STREAM:
            s = new (std::nothrow) stream_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_XPUB:
            s = new (std::nothrow) xpub_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_XSUB:
            s = new (std::nothrow) xsub_t (parent_, tid_, sid_);
            break;
        default:
            errno = EINVAL;
            return NULL;
    }

    alloc_assert (s);

    if (s->_mailbox == NULL) {
        s->lifecycle_coordinator ().mark_destroyed ();
        LIBZLINK_DELETE (s);
        return NULL;
    }

    return s;
}

zlink::socket_base_t::socket_base_t (ctx_t *parent_,
                                   uint32_t tid_,
                                   int sid_) :
    own_t (parent_, tid_),
    _tag (0xbaddecaf),
    _ctx_terminated (false),
    _runtime (),
    _auto_hwm_role (auto_hwm_role_none),
    _auto_hwm_role_override (false),
    _manual_sndhwm (false),
    _manual_rcvhwm (false),
    _manual_sndbuf (false),
    _manual_rcvbuf (false),
    _auto_hwm_context_plan (),
    _auto_hwm_socket_plan (),
    _local_peer_weight (100),
    _service_attachment (NULL),
    _channel_name_locked (false)
{
    _term_pipe_acks_registered = 0;
    _term_pipe_acks_received = 0;
    options.socket_id = sid_;
    options.ipv6 = (parent_->get (ZLINK_INTERNAL_OPT_IPV6) != 0);
    options.linger.store (parent_->get (ZLINK_INTERNAL_OPT_BLOCKY) ? -1 : 0);

    if (options.routing_id_size == 0) {
        unsigned char buf[16];
        generate_default_routing_id (buf);
        memcpy (options.routing_id, buf, sizeof buf);
        options.routing_id_size = static_cast<unsigned char> (sizeof buf);
    }

    mailbox_t *m = new (std::nothrow) mailbox_t ();
    zlink_assert (m);
    _mailbox = m;
}

void zlink::socket_base_t::set_auto_hwm_role (auto_hwm_role_t role_)
{
    _auto_hwm_role = role_;
    _auto_hwm_role_override = role_ != auto_hwm_role_none;
    refresh_auto_hwm_policy ();
}

void zlink::socket_base_t::refresh_auto_hwm_policy ()
{
    ctx_t *ctx = get_ctx ();
    if (!ctx)
        return;

    const bool enabled = ctx->get (ZLINK_CTX_OPT_AUTO_HWM_ENABLE) != 0;
    const int total_memory_budget_mb =
      ctx->get (ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB);
    auto_hwm_context_plan_from_budget_mb (enabled, total_memory_budget_mb,
                                          &_auto_hwm_context_plan);

    auto_hwm_role_t role = _auto_hwm_role_override ? _auto_hwm_role
                                                   : auto_hwm_role_none;
    if (role == auto_hwm_role_none)
        role = auto_hwm_default_role_for_socket_type (options.type);
    _auto_hwm_role = role;

    size_t managed_connections = 0;
    {
        scoped_lock_t lock (monitor_runtime ().sync);
        managed_connections = endpoint_runtime ().attached_pipe_count ();
    }
    auto_hwm_socket_plan_for_role (_auto_hwm_context_plan, role, options.type,
                                   managed_connections, managed_connections,
                                   &_auto_hwm_socket_plan);

    if (!_auto_hwm_context_plan.enabled)
        return;

    bool refresh_hwms = false;
    if (!_manual_sndhwm && options.sndhwm != _auto_hwm_socket_plan.sndhwm) {
        options.sndhwm = _auto_hwm_socket_plan.sndhwm;
        refresh_hwms = true;
    }
    if (!_manual_rcvhwm && options.rcvhwm != _auto_hwm_socket_plan.rcvhwm) {
        options.rcvhwm = _auto_hwm_socket_plan.rcvhwm;
        refresh_hwms = true;
    }
    if (!_manual_sndbuf)
        options.sndbuf = _auto_hwm_socket_plan.requested_sndbuf;
    if (!_manual_rcvbuf)
        options.rcvbuf = _auto_hwm_socket_plan.requested_rcvbuf;

    if (refresh_hwms)
        refresh_attached_pipe_hwms ();
}

static void copy_routing_id (zlink_routing_id_t *out_,
                             const zlink::blob_t &routing_id_)
{
    if (!out_)
        return;
    const size_t copy_size =
      std::min (routing_id_.size (), sizeof (out_->data));
    out_->size = static_cast<uint8_t> (copy_size);
    if (copy_size > 0)
        memcpy (out_->data, routing_id_.data (), copy_size);
}

zlink::socket_base_t::~socket_base_t ()
{
    LIBZLINK_DELETE (_service_attachment);
    if (_mailbox)
        LIBZLINK_DELETE (_mailbox);

    scoped_lock_t lock (monitor_runtime ().sync);
    stop_monitor ();

    zlink_assert (lifecycle_coordinator ().is_destroyed ());
}

zlink::i_mailbox *zlink::socket_base_t::get_mailbox () const
{
    return _mailbox;
}

void zlink::socket_base_t::store_socket_msg_part (
  std::vector<zlink_msg_t> *parts_,
  zlink::msg_t *msg_)
{
    if (!parts_ || !msg_)
        return;

    zlink_msg_t stored;
    memset (&stored, 0, sizeof (stored));
    zlink::msg_t *stored_msg = reinterpret_cast<zlink::msg_t *> (&stored);
    const int init_rc = stored_msg->init ();
    errno_assert (init_rc == 0);
    const int move_rc = stored_msg->move (*msg_);
    errno_assert (move_rc == 0);
    parts_->push_back (stored);
}

int zlink::socket_base_t::init_peer_weight_command (zlink::msg_t *msg_,
                                                    uint32_t weight_)
{
    if (!msg_) {
        errno = EFAULT;
        return -1;
    }

    const int rc = msg_->init_size (peer_weight_cmd_name_size + 4);
    if (rc != 0)
        return -1;

    memcpy (msg_->data (), peer_weight_cmd_name, peer_weight_cmd_name_size);
    unsigned char *payload =
      static_cast<unsigned char *> (msg_->data ()) + peer_weight_cmd_name_size;
    payload[0] = static_cast<unsigned char> ((weight_ >> 24) & 0xffu);
    payload[1] = static_cast<unsigned char> ((weight_ >> 16) & 0xffu);
    payload[2] = static_cast<unsigned char> ((weight_ >> 8) & 0xffu);
    payload[3] = static_cast<unsigned char> (weight_ & 0xffu);
    msg_->set_flags (zlink::msg_t::command);
    return 0;
}

bool zlink::socket_base_t::decode_peer_weight_command (const zlink::msg_t &msg_,
                                                       uint32_t *weight_out_)
{
    if (!(msg_.flags () & zlink::msg_t::command))
        return false;
    if (msg_.size () != peer_weight_cmd_name_size + 4)
        return false;
    if (memcmp (const_cast<zlink::msg_t &> (msg_).data (),
                peer_weight_cmd_name, peer_weight_cmd_name_size)
        != 0) {
        return false;
    }

    const unsigned char *payload =
      static_cast<unsigned char *> (const_cast<zlink::msg_t &> (msg_).data ())
      + peer_weight_cmd_name_size;
    const uint32_t weight = (static_cast<uint32_t> (payload[0]) << 24)
                            | (static_cast<uint32_t> (payload[1]) << 16)
                            | (static_cast<uint32_t> (payload[2]) << 8)
                            | static_cast<uint32_t> (payload[3]);
    if (weight > 100)
        return false;

    if (weight_out_)
        *weight_out_ = weight;
    return true;
}

void zlink::socket_base_t::stop ()
{
    //  Called by ctx when it is terminated (zlink_ctx_term).
    //  'stop' command is sent from the threads that called zlink_ctx_term to
    //  the thread owning the socket. This way, blocking call in the
    //  owner thread can be interrupted.
    send_stop ();
}


int zlink::socket_base_t::xsetsockopt (int, const void *, size_t)
{
    errno = EINVAL;
    return -1;
}

int zlink::socket_base_t::xgetsockopt (int, void *, size_t *)
{
    errno = EINVAL;
    return -1;
}

bool zlink::socket_base_t::xhas_out ()
{
    return false;
}

int zlink::socket_base_t::xsend (msg_t *)
{
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xsend_routed (const zlink_routing_id_t *target_rid_,
                                        msg_t *msg_)
{
    LIBZLINK_UNUSED (target_rid_);
    LIBZLINK_UNUSED (msg_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xrollback ()
{
    return 0;
}

bool zlink::socket_base_t::xhas_in ()
{
    return false;
}

int zlink::socket_base_t::xjoin (const char *group_)
{
    LIBZLINK_UNUSED (group_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xleave (const char *group_)
{
    LIBZLINK_UNUSED (group_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xrecv (msg_t *)
{
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xrecv_routed (msg_t *msg_,
                                        zlink_routing_id_t *source_rid_out_)
{
    if (source_rid_out_)
        source_rid_out_->size = 0;

    const int rc = xrecv (msg_);
    if (rc == 0 && source_rid_out_)
        copy_last_recv_source_rid (source_rid_out_);
    return rc;
}

void zlink::socket_base_t::xarm_socket_msg_dispatch ()
{
}

void zlink::socket_base_t::xread_activated (pipe_t *)
{
    zlink_assert (false);
}
void zlink::socket_base_t::xwrite_activated (pipe_t *)
{
    zlink_assert (false);
}

void zlink::socket_base_t::xhiccuped (pipe_t *)
{
    zlink_assert (false);
}
