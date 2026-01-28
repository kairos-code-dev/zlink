/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/thread_safe_socket.hpp"

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

#include <string.h>

zmq::thread_safe_socket_t::thread_safe_socket_t (ctx_t *ctx_,
                                                 socket_base_t *socket_) :
    _tag (threadsafe_socket_tag_value),
    _ctx (ctx_),
    _socket (socket_),
    _strand (ctx_->get_threadsafe_io_context ().get_executor ())
{
    zmq_assert (_ctx);
    zmq_assert (_socket);
}

zmq::thread_safe_socket_t::~thread_safe_socket_t ()
{
    zmq_assert (_socket == NULL);
    _tag = 0xdeadbeef;
}

bool zmq::thread_safe_socket_t::check_tag () const
{
    return _tag == threadsafe_socket_tag_value;
}

zmq::socket_base_t *zmq::thread_safe_socket_t::get_socket () const
{
    return _socket;
}

zmq::ctx_t *zmq::thread_safe_socket_t::get_ctx () const
{
    return _ctx;
}

int zmq::thread_safe_socket_t::close ()
{
    if (!_socket)
        return 0;

    int err = 0;
    const int rc = dispatch<int> (
      [this] () {
          if (!_socket)
              return 0;
          const int rc = _socket->close ();
          _socket = NULL;
          _tag = 0xdeadbeef;
          return rc;
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::setsockopt (int option_,
                                           const void *optval_,
                                           size_t optvallen_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, option_, optval_, optvallen_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->setsockopt (option_, optval_, optvallen_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::getsockopt (int option_,
                                           void *optval_,
                                           size_t *optvallen_)
{
    if (option_ == ZMQ_THREAD_SAFE) {
        if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
            errno = EINVAL;
            return -1;
        }
        const int value = 1;
        memcpy (optval_, &value, sizeof (value));
        *optvallen_ = sizeof (value);
        return 0;
    }

    int err = 0;
    const int rc = dispatch<int> (
      [this, option_, optval_, optvallen_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->getsockopt (option_, optval_, optvallen_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::bind (const char *endpoint_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, endpoint_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->bind (endpoint_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::connect (const char *endpoint_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, endpoint_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->connect (endpoint_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::term_endpoint (const char *endpoint_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, endpoint_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->term_endpoint (endpoint_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::send (msg_t *msg_, int flags_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, msg_, flags_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->send (msg_, flags_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::recv (msg_t *msg_, int flags_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, msg_, flags_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->recv (msg_, flags_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

bool zmq::thread_safe_socket_t::has_in ()
{
    return dispatch<bool> (
      [this] () { return _socket ? _socket->has_in () : false; }, NULL);
}

bool zmq::thread_safe_socket_t::has_out ()
{
    return dispatch<bool> (
      [this] () { return _socket ? _socket->has_out () : false; }, NULL);
}

int zmq::thread_safe_socket_t::join (const char *group_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, group_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->join (group_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::leave (const char *group_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, group_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->leave (group_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::monitor (const char *endpoint_,
                                        uint64_t events_,
                                        int event_version_,
                                        int type_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, endpoint_, events_, event_version_, type_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->monitor (endpoint_, events_, event_version_, type_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::socket_stats (zmq_socket_stats_t *stats_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, stats_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->socket_stats (stats_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::socket_peer_info (
  const zmq_routing_id_t *routing_id_, zmq_peer_info_t *info_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, routing_id_, info_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->socket_peer_info (routing_id_, info_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::socket_peer_routing_id (int index_,
                                                       zmq_routing_id_t *out_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, index_, out_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->socket_peer_routing_id (index_, out_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::socket_peer_count ()
{
    int err = 0;
    const int rc = dispatch<int> (
      [this] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->socket_peer_count ();
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::socket_peers (zmq_peer_info_t *peers_,
                                             size_t *count_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, peers_, count_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->socket_peers (peers_, count_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::get_peer_state (const void *routing_id_,
                                               size_t routing_id_size_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, routing_id_, routing_id_size_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->get_peer_state (routing_id_, routing_id_size_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}
