/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_XPUB_HPP_INCLUDED__
#define __ZLINK_XPUB_HPP_INCLUDED__

#include <deque>

#include "sockets/socket_base.hpp"
#include "core/session_base.hpp"
#include "utils/mtrie.hpp"
#include "sockets/dist.hpp"

namespace zlink
{
class ctx_t;
class msg_t;
class pipe_t;
class io_thread_t;

class xpub_t : public socket_base_t
{
  public:
    xpub_t (zlink::ctx_t *parent_,
            uint32_t tid_,
            int sid_);
    ~xpub_t () ZLINK_OVERRIDE;

    //  Implementations of virtual functions from socket_base_t.
    void xattach_pipe (zlink::pipe_t *pipe_,
                       bool subscribe_to_all_ = false,
                       bool locally_initiated_ = false) ZLINK_OVERRIDE;
    int xsend (zlink::msg_t *msg_) ZLINK_FINAL;
    bool xhas_out () ZLINK_FINAL;
    int xrecv (zlink::msg_t *msg_) ZLINK_OVERRIDE;
    bool xhas_in () ZLINK_OVERRIDE;
    void xread_activated (zlink::pipe_t *pipe_) ZLINK_FINAL;
    void xwrite_activated (zlink::pipe_t *pipe_) ZLINK_FINAL;
    int
    xsetsockopt (int option_, const void *optval_, size_t optvallen_) ZLINK_FINAL;
    int xgetsockopt (int option_, void *optval_, size_t *optvallen_) ZLINK_FINAL;
    void xpipe_terminated (zlink::pipe_t *pipe_) ZLINK_FINAL;

  private:
    //  Function to be applied to the trie to send all the subscriptions
    //  upstream.
    static void send_unsubscription (zlink::mtrie_t::prefix_t data_,
                                     size_t size_,
                                     xpub_t *self_);

    //  Function to be applied to each matching pipes.
    static void mark_as_matching (zlink::pipe_t *pipe_, xpub_t *self_);

    //  List of all subscriptions mapped to corresponding pipes.
    mtrie_t _subscriptions;

    //  List of manual subscriptions mapped to corresponding pipes.
    mtrie_t _manual_subscriptions;

    //  Distributor of messages holding the list of outbound pipes.
    dist_t _dist;

    // If true, send all subscription messages upstream, not just
    // unique ones
    bool _verbose_subs;

    // If true, send all unsubscription messages upstream, not just
    // unique ones
    bool _verbose_unsubs;

    //  True if we are in the middle of sending a multi-part message.
    bool _more_send;

    //  True if we are in the middle of receiving a multi-part message.
    bool _more_recv;

    //  If true, subscribe and cancel messages are processed for the rest
    //  of multipart message.
    bool _process_subscribe;

    //  This option is enabled with ZLINK_ONLY_FIRST_SUBSCRIBE.
    //  If true, messages following subscribe/unsubscribe in a multipart
    //  message are treated as user data regardless of the first byte.
    bool _only_first_subscribe;

    //  Drop messages if HWM reached, otherwise return with EAGAIN
    bool _lossy;

    //  Subscriptions will not bed added automatically, only after calling set option with ZLINK_SUBSCRIBE or ZLINK_UNSUBSCRIBE
    bool _manual;

    //  Send message to the last pipe, only used if xpub is on manual and after calling set option with ZLINK_SUBSCRIBE
    bool _send_last_pipe;

    //  Function to be applied to match the last pipe.
    static void mark_last_pipe_as_matching (zlink::pipe_t *pipe_, xpub_t *self_);

    //  Last pipe that sent subscription message, only used if xpub is on manual
    pipe_t *_last_pipe;

    // Pipes that sent subscriptions messages that have not yet been processed, only used if xpub is on manual
    std::deque<pipe_t *> _pending_pipes;

    //  Welcome message to send to pipe when attached
    msg_t _welcome_msg;

    //  List of pending (un)subscriptions, ie. those that were already
    //  applied to the trie, but not yet received by the user.
    std::deque<blob_t> _pending_data;
    std::deque<metadata_t *> _pending_metadata;
    std::deque<unsigned char> _pending_flags;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (xpub_t)
};
}

#endif
