/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_HPP_INCLUDED__
#define __ZLINK_STREAM_HPP_INCLUDED__

#include <deque>

#include "sockets/socket_base.hpp"
#include "sockets/fq.hpp"
#include "utils/blob.hpp"
#include "utils/stdint.hpp"

namespace zlink
{
class ctx_t;
class pipe_t;

class stream_t ZLINK_FINAL : public routing_socket_base_t
{
  public:
    stream_t (zlink::ctx_t *parent_, uint32_t tid_, int sid_);
    ~stream_t () ZLINK_OVERRIDE;

    void xattach_pipe (zlink::pipe_t *pipe_,
                       bool subscribe_to_all_,
                       bool locally_initiated_) ZLINK_FINAL;
    int xsend (zlink::msg_t *msg_) ZLINK_OVERRIDE;
    int xrecv (zlink::msg_t *msg_) ZLINK_OVERRIDE;
    bool xhas_in () ZLINK_OVERRIDE;
    bool xhas_out () ZLINK_OVERRIDE;
    void xread_activated (zlink::pipe_t *pipe_) ZLINK_FINAL;
    void xpipe_terminated (zlink::pipe_t *pipe_) ZLINK_FINAL;
    int xsetsockopt (int option_, const void *optval_, size_t optvallen_)
      ZLINK_FINAL;

  private:
    struct stream_event_t
    {
        blob_t routing_id;
        unsigned char code;
    };

    void identify_peer (pipe_t *pipe_, bool locally_initiated_);
    void queue_event (const blob_t &routing_id_, unsigned char code_);
    bool prefetch_event ();

    fq_t _fq;

    bool _prefetched;
    bool _routing_id_sent;
    msg_t _prefetched_routing_id;
    msg_t _prefetched_msg;

    zlink::pipe_t *_current_out;
    bool _more_out;

    uint32_t _next_integral_routing_id;

    std::deque<stream_event_t> _pending_events;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (stream_t)
};
}

#endif
