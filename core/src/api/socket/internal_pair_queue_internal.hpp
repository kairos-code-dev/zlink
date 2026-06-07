/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_INTERNAL_PAIR_QUEUE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_INTERNAL_PAIR_QUEUE_INTERNAL_HPP_INCLUDED__

#include "utils/precompiled.hpp"

#include <string>

#include "core/ctx.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
namespace internal_pair_queue
{
struct queue_t
{
    queue_t ();
    queue_t (const queue_t &) = delete;
    queue_t &operator= (const queue_t &) = delete;

    zlink::socket_base_t *rx_socket () const;
    zlink::socket_base_t *tx_socket () const;

  private:
    friend void close (queue_t *queue_);
    friend void close_and_wait (queue_t *queue_);
    friend int ensure (zlink::ctx_t *ctx_, const char *prefix_, queue_t *queue_);

    bool ready () const;
    void adopt (zlink::socket_base_t *rx_, zlink::socket_base_t *tx_, const char *endpoint_);
    void clear ();

    zlink::socket_base_t *_rx;
    zlink::socket_base_t *_tx;
    std::string _endpoint;
};

void close (queue_t *queue_);
void close_and_wait (queue_t *queue_);

int ensure (zlink::ctx_t *ctx_, const char *prefix_, queue_t *queue_);

int send_buffer_frame (zlink::socket_base_t *socket_, const void *data_, size_t size_, int flags_);

int recv_followup_with_retry (zlink::socket_base_t *socket_, zlink_msg_t *msg_, int flags_);
}
}

#endif
