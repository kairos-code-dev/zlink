/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_THREAD_SAFE_SOCKET_HPP_INCLUDED__
#define __ZMQ_THREAD_SAFE_SOCKET_HPP_INCLUDED__

#include <boost/asio.hpp>
#include <errno.h>
#include <stddef.h>

#include "utils/condition_variable.hpp"
#include "utils/macros.hpp"
#include "utils/mutex.hpp"
#include "zmq.h"

namespace zmq
{
class ctx_t;
class msg_t;
class socket_base_t;

static const uint32_t threadsafe_socket_tag_value = 0x7473736b;

class thread_safe_socket_t
{
  public:
    thread_safe_socket_t (ctx_t *ctx_, socket_base_t *socket_);
    ~thread_safe_socket_t ();

    bool check_tag () const;

    socket_base_t *get_socket () const;
    ctx_t *get_ctx () const;

    int close ();

    int setsockopt (int option_, const void *optval_, size_t optvallen_);
    int getsockopt (int option_, void *optval_, size_t *optvallen_);

    int bind (const char *endpoint_);
    int connect (const char *endpoint_);
    int term_endpoint (const char *endpoint_);

    int send (msg_t *msg_, int flags_);
    int recv (msg_t *msg_, int flags_);

    bool has_in ();
    bool has_out ();

    int join (const char *group_);
    int leave (const char *group_);

    int monitor (const char *endpoint_,
                 uint64_t events_,
                 int event_version_,
                 int type_);

    int socket_stats (zmq_socket_stats_t *stats_);
    int socket_peer_info (const zmq_routing_id_t *routing_id_,
                          zmq_peer_info_t *info_);
    int socket_peer_routing_id (int index_, zmq_routing_id_t *out_);
    int socket_peer_count ();
    int socket_peers (zmq_peer_info_t *peers_, size_t *count_);
    int get_peer_state (const void *routing_id_, size_t routing_id_size_);

  private:
    template <typename Result, typename Func>
    Result dispatch (Func func_, int *err_);

    uint32_t _tag;
    ctx_t *_ctx;
    socket_base_t *_socket;
    boost::asio::strand<boost::asio::io_context::executor_type> _strand;
    mutex_t _sync;
    condition_variable_t _sync_cv;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (thread_safe_socket_t)
};

template <typename Result, typename Func>
Result thread_safe_socket_t::dispatch (Func func_, int *err_)
{
    if (_strand.running_in_this_thread ()) {
        Result res = func_ ();
        if (err_)
            *err_ = errno;
        return res;
    }

    Result res = Result ();
    int err = 0;
    bool done = false;

    boost::asio::post (_strand, [this, func_, &res, &err, &done]() mutable {
        Result local_res = func_ ();
        int local_err = errno;

        scoped_lock_t lock (_sync);
        res = local_res;
        err = local_err;
        done = true;
        _sync_cv.broadcast ();
    });

    _sync.lock ();
    while (!done)
        _sync_cv.wait (&_sync, -1);
    _sync.unlock ();

    if (err_)
        *err_ = err;
    return res;
}
}

#endif
