/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DEALER_HPP_INCLUDED__
#define __ZLINK_DEALER_HPP_INCLUDED__

#include "sockets/socket_base.hpp"
#include "core/session_base.hpp"
#include "sockets/fq.hpp"
#include "sockets/lb.hpp"

namespace zlink
{
class ctx_t;
class msg_t;
class pipe_t;
class io_thread_t;
class socket_base_t;

class dealer_t : public socket_base_t
{
  public:
    dealer_t (zlink::ctx_t *parent_,
              uint32_t tid_,
              int sid_);
    ~dealer_t () ZLINK_OVERRIDE;

  protected:
    //  Overrides of functions from socket_base_t.
    void xattach_pipe (zlink::pipe_t *pipe_,
                       bool subscribe_to_all_,
                       bool locally_initiated_) ZLINK_FINAL;
    int xsetsockopt (int option_,
                     const void *optval_,
                     size_t optvallen_) ZLINK_OVERRIDE;
    int xsend (zlink::msg_t *msg_) ZLINK_OVERRIDE;
    int xrecv (zlink::msg_t *msg_) ZLINK_OVERRIDE;
    bool xhas_in () ZLINK_OVERRIDE;
    bool xhas_out () ZLINK_OVERRIDE;
    void xread_activated (zlink::pipe_t *pipe_) ZLINK_FINAL;
    void xwrite_activated (zlink::pipe_t *pipe_) ZLINK_FINAL;
    void xpipe_terminated (zlink::pipe_t *pipe_) ZLINK_OVERRIDE;

    //  Send and recv - knowing which pipe was used.
    int sendpipe (zlink::msg_t *msg_, zlink::pipe_t **pipe_);
    int recvpipe (zlink::msg_t *msg_, zlink::pipe_t **pipe_);

  private:
    //  Messages are fair-queued from inbound pipes. And load-balanced to
    //  the outbound pipes.
    fq_t _fq;
    lb_t _lb;

    // if true, send an empty message to every connected router peer
    bool _probe_router;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (dealer_t)
};
}

#endif
