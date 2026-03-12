/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_HPP_INCLUDED__
#define __ZLINK_STREAM_HPP_INCLUDED__

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "sockets/socket_base.hpp"
#include "sockets/fq.hpp"
#include "utils/fast_mutex.hpp"
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
    int stream_dispatch_start_raw (zlink_stream_on_raw_fn callback_)
      ZLINK_OVERRIDE;
    int stream_dispatch_stop () ZLINK_OVERRIDE;
    bool stream_dispatch_active () const ZLINK_OVERRIDE;
    bool stream_dispatch_in_callback () const ZLINK_OVERRIDE;
    int stream_dispatch_send_from_io (const zlink_routing_id_t *rid_,
                                      const void *data_,
                                      size_t size_,
                                      int flags_) ZLINK_OVERRIDE;
    int stream_dispatch_send_msg_from_io (const zlink_routing_id_t *rid_,
                                          zlink::msg_t *msg_,
                                          int flags_) ZLINK_OVERRIDE;
    std::recursive_mutex *api_sync_mutex () ZLINK_OVERRIDE;

  private:
    enum
    {
        route_shard_count = 64
    };

    struct route_shard_t
    {
        typedef std::map<uint32_t, zlink::pipe_t *> routes_t;

        fast_mutex_t sync;
        routes_t routes;
    };

    route_shard_t &route_shard_for (uint32_t routing_id_);
    void identify_peer (pipe_t *pipe_, bool locally_initiated_);
    uint32_t ensure_dispatch_routing_id (pipe_t *pipe_);
    void maybe_emit_connect_event (pipe_t *pipe_,
                                   uint32_t routing_id_value_ = 0);
    int xstream_dispatch_msg (zlink::msg_t *msg_, zlink::pipe_t *pipe_)
      ZLINK_OVERRIDE;
    uint32_t resolve_dispatch_routing_id_fast (const zlink::msg_t *msg_,
                                               zlink::pipe_t *pipe_);
    void stop_dispatch_from_callback ();

    fq_t _fq;

    zlink::pipe_t *_current_out;
    bool _more_out;

    std::atomic<uint32_t> _next_integral_routing_id;
    route_shard_t _route_shards[route_shard_count];

    std::atomic<bool> _dispatch_active;
    std::atomic<zlink_stream_on_raw_fn> _dispatch_raw_callback;
    mutable std::recursive_mutex _api_mutex;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (stream_t)
};
}

#endif
