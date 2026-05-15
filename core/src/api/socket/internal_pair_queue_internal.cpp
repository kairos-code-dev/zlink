/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/socket/internal_pair_queue_internal.hpp"
#include "core/recv_internal.hpp"
#include "utils/random.hpp"

namespace
{
void set_internal_pair_socket_defaults (zlink::socket_base_t *socket_)
{
    if (!socket_)
        return;

    const int linger = 0;
    socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
}

bool recv_internal_pair_handshake (zlink::socket_base_t *socket_,
                                   long timeout_ms_)
{
    if (!socket_)
        return false;

    const int timeout = static_cast<int> (timeout_ms_);
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout,
                             sizeof (timeout))
        != 0)
        return false;

    zlink::msg_t msg;
    if (msg.init () != 0)
        return false;

    const int rc = socket_->recv (&msg, 0);
    msg.close ();

    const int blocking = -1;
    (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &blocking,
                                sizeof (blocking));
    return rc == 0;
}

bool handshake_internal_pair (zlink::socket_base_t *rx_,
                              zlink::socket_base_t *tx_)
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

    if (!recv_internal_pair_handshake (rx_, 100))
        return false;

    if (msg.init_size (sizeof (ack)) != 0)
        return false;
    memcpy (msg.data (), &ack, sizeof (ack));
    if (rx_->send (&msg, 0) != 0) {
        msg.close ();
        return false;
    }
    msg.close ();

    if (!recv_internal_pair_handshake (tx_, 100))
        return false;

    return true;
}
}

void zlink::internal_pair_queue::close (queue_t *queue_)
{
    if (!queue_)
        return;

    if (queue_->tx) {
        queue_->tx->stop ();
        queue_->tx->close ();
        queue_->tx = NULL;
    }
    if (queue_->rx) {
        queue_->rx->stop ();
        queue_->rx->close ();
        queue_->rx = NULL;
    }
    queue_->endpoint.clear ();
}

void zlink::internal_pair_queue::close_and_wait (queue_t *queue_)
{
    if (!queue_)
        return;

    zlink::socket_base_t *tx = queue_->tx;
    zlink::socket_base_t *rx = queue_->rx;
    zlink::ctx_t *ctx = rx ? rx->get_ctx () : tx ? tx->get_ctx () : NULL;

    close (queue_);
    if (ctx && tx)
        (void) ctx->wait_for_socket_removal (tx, 1000);
    if (ctx && rx)
        (void) ctx->wait_for_socket_removal (rx, 1000);
}

int zlink::internal_pair_queue::ensure (zlink::ctx_t *ctx_,
                                        const char *prefix_,
                                        queue_t *queue_)
{
    if (!ctx_ || !prefix_ || !queue_) {
        errno = EFAULT;
        return -1;
    }
    if (queue_->rx && queue_->tx)
        return 0;

    char endpoint[128];
    snprintf (endpoint, sizeof (endpoint), "inproc://%s-%p-%u", prefix_,
              static_cast<void *> (queue_), zlink::generate_random ());

    zlink::socket_base_t *rx = ctx_->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *tx = ctx_->create_socket (ZLINK_CORE_SOCKET_PAIR);
    if (!rx || !tx) {
        if (tx) {
            tx->stop ();
            tx->close ();
        }
        if (rx) {
            rx->stop ();
            rx->close ();
        }
        return -1;
    }

    set_internal_pair_socket_defaults (rx);
    set_internal_pair_socket_defaults (tx);

    if (rx->bind (endpoint) != 0 || tx->connect (endpoint) != 0) {
        const int saved_errno = errno;
        tx->stop ();
        tx->close ();
        rx->stop ();
        rx->close ();
        errno = saved_errno;
        return -1;
    }

    if (!handshake_internal_pair (rx, tx)) {
        const int saved_errno = errno != 0 ? errno : EPROTO;
        tx->stop ();
        tx->close ();
        rx->stop ();
        rx->close ();
        errno = saved_errno;
        return -1;
    }

    queue_->rx = rx;
    queue_->tx = tx;
    queue_->endpoint = endpoint;
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

int zlink::internal_pair_queue::recv_followup_with_retry (
  zlink::socket_base_t *socket_,
  zlink_msg_t *msg_,
  int flags_)
{
    return zlink::recv_followup_msg_socket_wait (socket_, msg_, flags_);
}
