/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "api/service_handle_internal.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "core/multipart_send_txn.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/routing_id.hpp"

#include <string.h>
namespace zlink
{
static const uint32_t spot_pub_tag_value = 0x1e6700db;

namespace
{
static void preserve_first_error (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}
}

namespace
{
static void spot_pub_send_ready_adapter (void *subject_, void *)
{
    spot_pub_t *pub = static_cast<spot_pub_t *> (subject_);
    if (pub)
        pub->dispatch_send_ready ();
}
}

spot_pub_t::spot_pub_t (spot_node_t *node_,
                        socket_base_t *socket_,
                        uint64_t attachment_id_,
                        bool node_owned_default_) :
    _node (node_),
    _socket (socket_),
    _runtime (node_ ? node_->runtime () : NULL),
    _attachment_id (attachment_id_),
    _tag (spot_pub_tag_value),
    _node_owned_default (node_owned_default_),
    _routing_id_locked (false),
    _send_ready_handler (NULL),
    _send_ready_subject (NULL),
    _send_ready_userdata (NULL),
    _destroying (false)
{
    memset (&_routing_id, 0, sizeof (_routing_id));
    initialize_routing_id (&_routing_id);
    register_spot_pub_side_handle (this);
}

spot_pub_t::~spot_pub_t ()
{
    erase_spot_pub_side_handle (this);
    _tag = 0xdeadbeef;
}

bool spot_pub_t::check_tag () const
{
    return _tag == spot_pub_tag_value;
}

bool spot_pub_t::is_node_owned_default () const
{
    return _node_owned_default;
}

socket_base_t *spot_pub_t::socket () const
{
    return _socket;
}

int spot_pub_t::initialize_routing_id (zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    generate_random_uuid_routing_id (out_);
    return 0;
}

void spot_pub_t::lock_routing_id ()
{
    _routing_id_locked.store (true, std::memory_order_release);
}

void spot_pub_t::submit_error_summary (int error_code_)
{
    if (_node)
        _node->submit_pub_summary (this, ZLINK_TOPOLOGY_STATE_ERROR,
                                   error_code_);
}

int spot_pub_t::publish (const char *topic_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         int flags_)
{
    socket_base_t *socket = this->socket ();
    if (!_node || !socket) {
        errno = EFAULT;
        return -1;
    }
    if (!topic_ || topic_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    const size_t topic_size = strlen (topic_);
    if (spot_control_protocol::is_reserved_subject (topic_, topic_size)) {
        errno = EINVAL;
        return -1;
    }
    if (part_count_ > 0 && !parts_) {
        errno = EINVAL;
        return -1;
    }
    if (!_runtime || _runtime->ensure_healthy () != 0)
        return -1;

    lock_routing_id ();

    scoped_lock_t publish_lock (_publish_sync);
    const int rc = zlink::logical_multipart_publish (
      socket, topic_, parts_, part_count_, flags_, true);
    const int saved_errno = rc == 0 ? 0 : errno;

    if (saved_errno != 0) {
        if (saved_errno != EAGAIN)
            submit_error_summary (saved_errno);
        errno = saved_errno;
        return -1;
    }

    return 0;
}

int spot_pub_t::set_option (int option_,
                            const void *optval_,
                            size_t optvallen_)
{
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    int socket_option = -1;
    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
            socket_option = ZLINK_INTERNAL_OPT_SNDHWM;
            break;
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
            socket_option = ZLINK_INTERNAL_OPT_SNDTIMEO;
            break;
        case ZLINK_SPOT_PUB_OPT_LINGER:
            socket_option = ZLINK_INTERNAL_OPT_LINGER;
            break;
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
            socket_option = ZLINK_INTERNAL_OPT_SNDBUF;
            break;
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
            socket_option = ZLINK_INTERNAL_OPT_RCVBUF;
            break;
        case ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
            socket_option = ZLINK_INTERNAL_OPT_AUTO_HWM_MSG_UNIT_BYTES;
            break;
        case ZLINK_SPOT_PUB_OPT_NODROP:
            socket_option = ZLINK_INTERNAL_OPT_XPUB_NODROP;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    scoped_lock_t lock (_sync);
    const int rc = socket->setsockopt (socket_option, optval_, optvallen_);
    return rc;
}

int spot_pub_t::set_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_routing_id_locked.load (std::memory_order_acquire)) {
        errno = EFSM;
        return -1;
    }
    _routing_id.size = static_cast<uint8_t> (size_);
    memcpy (_routing_id.data, data_, size_);
    return 0;
}

int spot_pub_t::set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                        void *subject_,
                                        void *userdata_)
{
    socket_base_t *socket = this->socket ();
    if (!socket || !handler_ || !subject_) {
        errno = EINVAL;
        return -1;
    }

    if (socket->socket_set_send_ready_handler_ex (&spot_pub_send_ready_adapter,
                                                  this)
        != 0)
        return -1;

    _send_ready_userdata.store (userdata_, std::memory_order_release);
    _send_ready_subject.store (subject_, std::memory_order_release);
    _send_ready_handler.store (handler_, std::memory_order_release);
    return 0;
}

int spot_pub_t::routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    *out_ = _routing_id;
    return 0;
}

int spot_pub_t::fill_monitor_snapshot (zlink_monitor_snapshot_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (socket->monitor_snapshot (out_) != 0)
        return -1;
    out_->source_kind = ZLINK_MONITOR_SOURCE_SPOT_PUB;
    out_->state_flags &= ~ZLINK_MONITOR_STATE_READY;
    return 0;
}

bool spot_pub_t::owns_socket (const socket_base_t *socket_) const
{
    return socket_ && socket_ == socket ();
}

void spot_pub_t::invoke_send_ready_for_testing ()
{
    socket_base_t *pub_socket = socket ();
    if (pub_socket)
        pub_socket->invoke_send_ready_handler_for_testing ();
}

void spot_pub_t::emit_ready_event ()
{
}

void spot_pub_t::dispatch_send_ready ()
{
    if (_node) {
        service_public_api_scope_t admission (_node->public_api_guard ());
        if (!admission.acquired ())
            return;
    }
    zlink_send_ready_handler_fn handler =
      _send_ready_handler.load (std::memory_order_acquire);
    void *subject = _send_ready_subject.load (std::memory_order_acquire);
    if (handler && subject)
        handler (subject,
                 _send_ready_userdata.load (std::memory_order_acquire));
}

int spot_pub_t::destroy_internal (bool allow_embedded_default_,
                                  bool notify_node_)
{
    if (_node_owned_default && !allow_embedded_default_) {
        errno = EINVAL;
        return -1;
    }

    _destroying.store (true, std::memory_order_release);

    socket_base_t *socket = this->socket ();
    int first_error = 0;
    const bool node_shutting_down = _node && _node->is_shutting_down ();

    if (notify_node_ && _node)
        _node->remove_spot_pub (this);
    if (notify_node_ && _node)
        _node->submit_pub_summary (this, ZLINK_TOPOLOGY_STATE_STOPPED, 0);

    if (socket) {
        if (_node)
            preserve_first_error (
              !node_shutting_down ? _node->destroy_attachment (_attachment_id)
                                 : _node->destroy_attachment_async (
                                     _attachment_id),
              &first_error);
        else {
            socket->stop ();
            socket->close ();
        }
    }
    _socket = NULL;
    _runtime = NULL;
    _attachment_id = 0;
    _node = NULL;
    _node_owned_default = false;
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

int spot_pub_t::destroy ()
{
    return destroy_internal (false, true);
}

int spot_pub_t::destroy_from_node ()
{
    return destroy_internal (true, true);
}

int spot_pub_t::abort_create ()
{
    return destroy_internal (true, false);
}
}
