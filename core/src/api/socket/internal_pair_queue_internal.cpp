/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/socket/internal_pair_queue_internal.hpp"
#include "core/recv_internal.hpp"
#include "utils/random.hpp"

#include <cstdarg>
#include <cstdlib>
#include <stdio.h>

namespace
{
const long internal_pair_handshake_timeout_ms = 100;
const int internal_pair_socket_removal_timeout_ms = 1000;

void debug_internal_pair_queue (const char *fmt_, ...)
{
    const char *enabled = std::getenv ("ZLINK_DEBUG_INTERNAL_PAIR_QUEUE");
    if (!enabled || enabled[0] == '\0')
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[internal-pair-queue] ");
    std::vfprintf (stderr, fmt_, args);
    va_end (args);
}

void set_internal_pair_socket_defaults (zlink::socket_base_t *socket_)
{
    if (!socket_)
        return;

    const int linger = 0;
    socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
}

bool recv_internal_pair_handshake (zlink::socket_base_t *socket_, long timeout_ms_)
{
    if (!socket_)
        return false;

    const int timeout = static_cast<int> (timeout_ms_);
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout, sizeof (timeout)) != 0)
        return false;

    zlink::msg_t msg;
    if (msg.init () != 0)
        return false;

    const int rc = socket_->recv (&msg, 0);
    msg.close ();

    const int blocking = -1;
    (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &blocking, sizeof (blocking));
    return rc == 0;
}

void close_internal_pair_socket (zlink::socket_base_t *socket_)
{
    if (!socket_)
        return;
    socket_->stop ();
    socket_->close ();
}

void close_internal_pair_sockets (zlink::socket_base_t *rx_, zlink::socket_base_t *tx_)
{
    close_internal_pair_socket (tx_);
    close_internal_pair_socket (rx_);
}

bool handshake_internal_pair (zlink::socket_base_t *rx_, zlink::socket_base_t *tx_)
{
    if (!rx_ || !tx_)
        return false;

    const unsigned char hello = 0x11;
    const unsigned char ack = 0x22;

    zlink::msg_t msg;
    if (msg.init_size (sizeof (hello)) != 0)
        return false;
    memcpy (msg.data (), &hello, sizeof (hello));
    if (tx_->send (&msg, 0) != 0) {
        msg.close ();
        return false;
    }
    msg.close ();

    if (!recv_internal_pair_handshake (rx_, internal_pair_handshake_timeout_ms))
        return false;

    if (msg.init_size (sizeof (ack)) != 0)
        return false;
    memcpy (msg.data (), &ack, sizeof (ack));
    if (rx_->send (&msg, 0) != 0) {
        msg.close ();
        return false;
    }
    msg.close ();

    if (!recv_internal_pair_handshake (tx_, internal_pair_handshake_timeout_ms))
        return false;

    return true;
}
}

zlink::internal_pair_queue::queue_t::queue_t () : _rx (NULL), _tx (NULL)
{
}

zlink::socket_base_t *zlink::internal_pair_queue::queue_t::rx_socket () const
{
    return _rx;
}

zlink::socket_base_t *zlink::internal_pair_queue::queue_t::tx_socket () const
{
    return _tx;
}

bool zlink::internal_pair_queue::queue_t::ready () const
{
    return _rx && _tx;
}

void zlink::internal_pair_queue::queue_t::adopt (zlink::socket_base_t *rx_,
                                                 zlink::socket_base_t *tx_,
                                                 const char *endpoint_)
{
    _rx = rx_;
    _tx = tx_;
    _endpoint = endpoint_ ? endpoint_ : "";
}

void zlink::internal_pair_queue::queue_t::clear ()
{
    _rx = NULL;
    _tx = NULL;
    _endpoint.clear ();
}

void zlink::internal_pair_queue::close (queue_t *queue_)
{
    if (!queue_)
        return;

    debug_internal_pair_queue ("close queue=%p rx=%d tx=%d endpoint=%s\n",
                               static_cast<void *> (queue_),
                               queue_->_rx ? queue_->_rx->socket_id () : -1,
                               queue_->_tx ? queue_->_tx->socket_id () : -1,
                               queue_->_endpoint.c_str ());
    if (queue_->_tx) {
        close_internal_pair_socket (queue_->_tx);
        queue_->_tx = NULL;
    }
    if (queue_->_rx) {
        close_internal_pair_socket (queue_->_rx);
        queue_->_rx = NULL;
    }
    queue_->clear ();
}

void zlink::internal_pair_queue::close_and_wait (queue_t *queue_)
{
    if (!queue_)
        return;

    zlink::socket_base_t *tx = queue_->_tx;
    zlink::socket_base_t *rx = queue_->_rx;
    zlink::ctx_t *ctx = rx ? rx->get_ctx () : tx ? tx->get_ctx () : NULL;

    close (queue_);
    if (ctx && tx)
        (void) ctx->wait_for_socket_removal (tx, internal_pair_socket_removal_timeout_ms);
    if (ctx && rx)
        (void) ctx->wait_for_socket_removal (rx, internal_pair_socket_removal_timeout_ms);
}

int zlink::internal_pair_queue::ensure (zlink::ctx_t *ctx_, const char *prefix_, queue_t *queue_)
{
    if (!ctx_ || !prefix_ || !queue_) {
        errno = EFAULT;
        return -1;
    }
    if (queue_->ready ())
        return 0;

    char endpoint[128];
    snprintf (endpoint, sizeof (endpoint), "inproc://%s-%p-%u", prefix_,
              static_cast<void *> (queue_), zlink::generate_random ());

    zlink::socket_base_t *rx = ctx_->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *tx = ctx_->create_socket (ZLINK_CORE_SOCKET_PAIR);
    if (!rx || !tx) {
        close_internal_pair_sockets (rx, tx);
        return -1;
    }

    debug_internal_pair_queue ("ensure prefix=%s queue=%p rx=%d tx=%d endpoint=%s\n", prefix_,
                               static_cast<void *> (queue_), rx->socket_id (), tx->socket_id (),
                               endpoint);

    set_internal_pair_socket_defaults (rx);
    set_internal_pair_socket_defaults (tx);

    if (rx->bind (endpoint) != 0 || tx->connect (endpoint) != 0) {
        const int saved_errno = errno;
        close_internal_pair_sockets (rx, tx);
        errno = saved_errno;
        return -1;
    }

    if (!handshake_internal_pair (rx, tx)) {
        const int saved_errno = errno != 0 ? errno : EPROTO;
        close_internal_pair_sockets (rx, tx);
        errno = saved_errno;
        return -1;
    }

    queue_->adopt (rx, tx, endpoint);
    errno = 0;
    return 0;
}

int zlink::internal_pair_queue::send_buffer_frame (zlink::socket_base_t *socket_,
                                                   const void *data_,
                                                   size_t size_,
                                                   int flags_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }

    zlink::msg_t msg;
    if (msg.init_size (size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (msg.data (), data_, size_);
    const int rc = socket_->send (&msg, flags_);
    const int saved_errno = errno;
    msg.close ();
    errno = saved_errno;
    return rc;
}

int zlink::internal_pair_queue::recv_followup_with_retry (zlink::socket_base_t *socket_,
                                                          zlink_msg_t *msg_,
                                                          int flags_)
{
    return zlink::recv_followup_msg_socket_wait (socket_, msg_, flags_);
}
