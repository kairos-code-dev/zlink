/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/thread_safe_socket.hpp"

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

#include <algorithm>
#include <limits>
#include <stdlib.h>
#include <string.h>
#include <utility>

zmq::thread_safe_socket_t::thread_safe_socket_t (ctx_t *ctx_,
                                                 socket_base_t *socket_) :
    _tag (threadsafe_socket_tag_value),
    _ctx (ctx_),
    _socket (socket_),
    _strand (ctx_->get_threadsafe_io_context ().get_executor ()),
    _request_timer (ctx_->get_threadsafe_io_context ()),
    _request_pump_active (false),
    _next_request_id (1),
    _request_handler (NULL),
    _in_request_handler (false),
    _current_request_id (0)
{
    zmq_assert (_ctx);
    zmq_assert (_socket);
    _current_routing_id.size = 0;
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
          cancel_all_requests_internal (ECANCELED);
          _request_handler = NULL;
          _request_pump_active = false;
          _request_timer.cancel ();
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

void zmq::thread_safe_socket_t::wait_for_idle ()
{
    _sync.lock ();
    while (_active_calls.get () != 0)
        _sync_cv.wait (&_sync, -1);
    _sync.unlock ();
}

void zmq::thread_safe_socket_t::add_active ()
{
    _active_calls.add (1);
}

void zmq::thread_safe_socket_t::release_active ()
{
    if (_active_calls.sub (1))
        return;
    scoped_lock_t lock (_sync);
    _sync_cv.broadcast ();
}

zmq::thread_safe_socket_t::routing_id_key_t
zmq::thread_safe_socket_t::make_routing_id_key (
  const zmq_routing_id_t &rid_) const
{
    routing_id_key_t key;
    key.size = rid_.size;
    if (key.size > 0)
        memcpy (key.data, rid_.data, key.size);
    return key;
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
          if (_request_handler) {
              errno = EBUSY;
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

int zmq::thread_safe_socket_t::socket_stats_ex (zmq_socket_stats_ex_t *stats_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, stats_] () {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return _socket->socket_stats_ex (stats_);
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

int zmq::thread_safe_socket_t::get_socket_type ()
{
    if (!_socket) {
        errno = ENOTSOCK;
        return -1;
    }
    int type = 0;
    size_t size = sizeof (type);
    if (_socket->getsockopt (ZMQ_TYPE, &type, &size) != 0)
        return -1;
    return type;
}

bool zmq::thread_safe_socket_t::get_request_correlate ()
{
    int value = 1;
    size_t size = sizeof (value);
    if (!_socket)
        return true;
    if (_socket->getsockopt (ZMQ_REQUEST_CORRELATE, &value, &size) != 0)
        return true;
    return value != 0;
}

int zmq::thread_safe_socket_t::get_request_timeout ()
{
    int value = 5000;
    size_t size = sizeof (value);
    if (!_socket)
        return value;
    if (_socket->getsockopt (ZMQ_REQUEST_TIMEOUT, &value, &size) != 0)
        return 5000;
    return value;
}

bool zmq::thread_safe_socket_t::validate_request_params (
  int socket_type_,
  const zmq_routing_id_t *routing_id_,
  zmq_msg_t *parts_,
  size_t part_count_,
  zmq_request_cb_fn callback_,
  bool require_callback_)
{
    if (socket_type_ != ZMQ_ROUTER && socket_type_ != ZMQ_DEALER) {
        errno = ENOTSUP;
        return false;
    }
    if (require_callback_ && !callback_) {
        errno = EINVAL;
        return false;
    }
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return false;
    }
    if (socket_type_ == ZMQ_ROUTER) {
        if (!routing_id_ || routing_id_->size == 0) {
            errno = EINVAL;
            return false;
        }
    } else if (routing_id_ && routing_id_->size > 0) {
        errno = EINVAL;
        return false;
    }
    return true;
}

bool zmq::thread_safe_socket_t::copy_parts_in (zmq_msg_t *parts_,
                                               size_t part_count_,
                                               std::vector<msg_t> *out_)
{
    out_->clear ();
    if (part_count_ == 0)
        return true;
    out_->resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        msg_t &dst = (*out_)[i];
        if (dst.init () != 0) {
            close_parts (out_);
            return false;
        }
        msg_t &src = *reinterpret_cast<msg_t *> (&parts_[i]);
        if (dst.copy (src) != 0) {
            close_parts (out_);
            return false;
        }
    }
    return true;
}

void zmq::thread_safe_socket_t::close_msg_array (zmq_msg_t *parts_,
                                                 size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zmq_msg_close (&parts_[i]);
}

void zmq::thread_safe_socket_t::close_parts (std::vector<msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (*parts_)[i].close ();
    parts_->clear ();
}

zmq_msg_t *
zmq::thread_safe_socket_t::alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                                  size_t *count_)
{
    if (count_)
        *count_ = 0;
    if (!parts_ || parts_->empty ())
        return NULL;

    const size_t count = parts_->size ();
    zmq_msg_t *out =
      static_cast<zmq_msg_t *> (malloc (count * sizeof (zmq_msg_t)));
    if (!out) {
        errno = ENOMEM;
        close_parts (parts_);
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        msg_t *dst = reinterpret_cast<msg_t *> (&out[i]);
        if (dst->init () != 0 || dst->move ((*parts_)[i]) != 0) {
            for (size_t j = 0; j <= i; ++j)
                zmq_msg_close (&out[j]);
            free (out);
            close_parts (parts_);
            errno = EFAULT;
            return NULL;
        }
    }
    parts_->clear ();
    if (count_)
        *count_ = count;
    return out;
}

uint64_t zmq::thread_safe_socket_t::request (const zmq_routing_id_t *routing_id_,
                                             zmq_msg_t *parts_,
                                             size_t part_count_,
                                             zmq_request_cb_fn callback_,
                                             int timeout_ms_)
{
    int err = 0;
    const uint64_t rc = dispatch<uint64_t> (
      [this, routing_id_, parts_, part_count_, callback_, timeout_ms_]() {
          return request_common (routing_id_, 0, parts_, part_count_,
                                 callback_, timeout_ms_, true);
      },
      &err);
    if (rc == 0)
        errno = err;
    return rc;
}

uint64_t
zmq::thread_safe_socket_t::group_request (const zmq_routing_id_t *routing_id_,
                                          uint64_t group_id_,
                                          zmq_msg_t *parts_,
                                          size_t part_count_,
                                          zmq_request_cb_fn callback_,
                                          int timeout_ms_)
{
    int err = 0;
    const uint64_t rc = dispatch<uint64_t> (
      [this, routing_id_, group_id_, parts_, part_count_, callback_,
       timeout_ms_]() {
          return request_common (routing_id_, group_id_, parts_, part_count_,
                                 callback_, timeout_ms_, true);
      },
      &err);
    if (rc == 0)
        errno = err;
    return rc;
}

uint64_t
zmq::thread_safe_socket_t::request_send (const zmq_routing_id_t *routing_id_,
                                         zmq_msg_t *parts_,
                                         size_t part_count_)
{
    int err = 0;
    const uint64_t rc = dispatch<uint64_t> (
      [this, routing_id_, parts_, part_count_]() {
          return request_common (routing_id_, 0, parts_, part_count_, NULL,
                                 -1, false);
      },
      &err);
    if (rc == 0)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::on_request (zmq_server_cb_fn handler_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, handler_]() {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          const int type = get_socket_type ();
          if (type != ZMQ_ROUTER && type != ZMQ_DEALER) {
              errno = ENOTSUP;
              return -1;
          }
          _request_handler = handler_;
          if (_request_handler)
              ensure_request_pump ();
          else if (_pending_requests.empty ()) {
              _request_pump_active = false;
              _request_timer.cancel ();
          }
          return 0;
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::reply (const zmq_routing_id_t *routing_id_,
                                      uint64_t request_id_,
                                      zmq_msg_t *parts_,
                                      size_t part_count_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, routing_id_, request_id_, parts_, part_count_]() {
          return reply_common (routing_id_, request_id_, parts_, part_count_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::reply_simple (zmq_msg_t *parts_,
                                             size_t part_count_)
{
    int err = 0;
    const int rc = dispatch<int> (
      [this, parts_, part_count_]() {
          if (!_in_request_handler) {
              errno = EINVAL;
              return -1;
          }
          const zmq_routing_id_t *rid =
            _current_routing_id.size > 0 ? &_current_routing_id : NULL;
          return reply_common (rid, _current_request_id, parts_, part_count_);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::request_recv (zmq_completion_t *completion_,
                                             int timeout_ms_)
{
    struct active_guard_t
    {
        active_guard_t (thread_safe_socket_t *self_) : self (self_)
        {
            self->add_active ();
        }
        ~active_guard_t () { self->release_active (); }
        thread_safe_socket_t *self;
    } guard (this);

    if (!completion_) {
        errno = EINVAL;
        return -1;
    }
    if (_strand.running_in_this_thread ()) {
        errno = EAGAIN;
        return -1;
    }
    if (timeout_ms_ < -1) {
        errno = EINVAL;
        return -1;
    }

    completion_->request_id = 0;
    completion_->parts = NULL;
    completion_->part_count = 0;
    completion_->error = 0;

    _completion_sync.lock ();
    if (timeout_ms_ == 0 && _completion_queue.empty ()) {
        _completion_sync.unlock ();
        errno = EAGAIN;
        return -1;
    }

    const std::chrono::steady_clock::time_point deadline =
      timeout_ms_ > 0 ? std::chrono::steady_clock::now ()
                            + std::chrono::milliseconds (timeout_ms_)
                      : std::chrono::steady_clock::time_point ();

    while (_completion_queue.empty ()) {
        if (timeout_ms_ < 0) {
            _completion_cv.wait (&_completion_sync, -1);
        } else {
            const auto now = std::chrono::steady_clock::now ();
            if (now >= deadline) {
                _completion_sync.unlock ();
                errno = ETIMEDOUT;
                return -1;
            }
            const auto remaining =
              std::chrono::duration_cast<std::chrono::milliseconds> (deadline
                                                                     - now);
            int rc = _completion_cv.wait (&_completion_sync,
                                          static_cast<int> (remaining.count ()));
            if (rc == -1 && errno == EAGAIN && _completion_queue.empty ()) {
                _completion_sync.unlock ();
                errno = ETIMEDOUT;
                return -1;
            }
        }
    }

    completion_entry_t entry = std::move (_completion_queue.front ());
    _completion_queue.pop_front ();
    _completion_sync.unlock ();

    completion_->request_id = entry.request_id;
    completion_->error = entry.error;
    if (entry.error != 0) {
        close_parts (&entry.parts);
        return 0;
    }

    if (entry.parts.empty ())
        return 0;

    zmq_msg_t *out =
      static_cast<zmq_msg_t *> (malloc (entry.parts.size ()
                                        * sizeof (zmq_msg_t)));
    if (!out) {
        _completion_sync.lock ();
        _completion_queue.push_front (std::move (entry));
        _completion_sync.unlock ();
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < entry.parts.size (); ++i) {
        msg_t *dst = reinterpret_cast<msg_t *> (&out[i]);
        if (dst->init () != 0 || dst->move (entry.parts[i]) != 0) {
            for (size_t j = 0; j <= i; ++j)
                zmq_msg_close (&out[j]);
            free (out);
            close_parts (&entry.parts);
            errno = EFAULT;
            return -1;
        }
    }

    completion_->parts = out;
    completion_->part_count = entry.parts.size ();
    return 0;
}

int zmq::thread_safe_socket_t::pending_requests ()
{
    int err = 0;
    const int rc = dispatch<int> (
      [this]() {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return static_cast<int> (_pending_requests.size ());
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

int zmq::thread_safe_socket_t::cancel_all_requests ()
{
    int err = 0;
    const int rc = dispatch<int> (
      [this]() {
          if (!_socket) {
              errno = ENOTSOCK;
              return -1;
          }
          return cancel_all_requests_internal (ECANCELED);
      },
      &err);
    if (rc == -1)
        errno = err;
    return rc;
}

uint64_t zmq::thread_safe_socket_t::request_common (
  const zmq_routing_id_t *routing_id_,
  uint64_t group_id_,
  zmq_msg_t *parts_,
  size_t part_count_,
  zmq_request_cb_fn callback_,
  int timeout_ms_,
  bool use_callback_)
{
    if (!_socket) {
        errno = ENOTSOCK;
        return 0;
    }

    const int socket_type = get_socket_type ();
    if (socket_type != ZMQ_ROUTER && socket_type != ZMQ_DEALER) {
        errno = ENOTSUP;
        return 0;
    }
    if (!validate_request_params (socket_type, routing_id_, parts_, part_count_,
                                  callback_, use_callback_))
        return 0;

    int timeout = timeout_ms_;
    if (timeout_ms_ == ZMQ_REQUEST_TIMEOUT_DEFAULT)
        timeout = get_request_timeout ();
    if (timeout < -1) {
        errno = EINVAL;
        return 0;
    }

    uint64_t request_id = _next_request_id++;
    if (request_id == 0)
        request_id = _next_request_id++;

    bool send_now = true;
    group_queue_t *queue = NULL;
    if (group_id_ != 0) {
        queue = &_group_queues[group_id_];
        if (queue->inflight_id != 0)
            send_now = false;
    }

    std::vector<msg_t> copied;
    if (!send_now) {
        if (!copy_parts_in (parts_, part_count_, &copied))
            return 0;
    }

    pending_request_t req;
    req.request_id = request_id;
    req.group_id = group_id_;
    req.has_routing_id = routing_id_ && routing_id_->size > 0;
    if (req.has_routing_id)
        req.routing_id = *routing_id_;
    else
        req.routing_id.size = 0;
    req.use_callback = use_callback_;
    req.callback = callback_;
    if (!send_now)
        req.parts.swap (copied);
    req.has_deadline = timeout >= 0;
    if (req.has_deadline)
        req.deadline = std::chrono::steady_clock::now ()
                       + std::chrono::milliseconds (timeout);
    req.correlate = get_request_correlate ();

    if (_pending_requests.find (request_id) != _pending_requests.end ()) {
        close_parts (&req.parts);
        errno = EAGAIN;
        return 0;
    }

    _pending_requests.insert (std::make_pair (request_id, std::move (req)));
    pending_request_t &stored = _pending_requests.find (request_id)->second;

    if (queue) {
        if (send_now)
            queue->inflight_id = request_id;
        else
            queue->pending.push_back (request_id);
    }

    if (send_now) {
        if (send_request_direct (stored, parts_, part_count_, socket_type)
            != 0) {
            if (queue) {
                if (queue->inflight_id == request_id)
                    queue->inflight_id = 0;
                if (queue->pending.empty ())
                    _group_queues.erase (group_id_);
            }
            _pending_requests.erase (request_id);
            return 0;
        }

        if (!stored.correlate)
            enqueue_non_correlate (stored);
    }

    close_msg_array (parts_, part_count_);
    ensure_request_pump ();
    return request_id;
}

int zmq::thread_safe_socket_t::reply_common (const zmq_routing_id_t *routing_id_,
                                             uint64_t request_id_,
                                             zmq_msg_t *parts_,
                                             size_t part_count_)
{
    if (!_socket) {
        errno = ENOTSOCK;
        return -1;
    }

    const int socket_type = get_socket_type ();
    if (socket_type != ZMQ_ROUTER && socket_type != ZMQ_DEALER) {
        errno = ENOTSUP;
        return -1;
    }
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (socket_type == ZMQ_ROUTER) {
        if (!routing_id_ || routing_id_->size == 0) {
            errno = EINVAL;
            return -1;
        }
    } else if (routing_id_ && routing_id_->size > 0) {
        errno = EINVAL;
        return -1;
    }

    const bool correlate = get_request_correlate ();
    const bool send_id = correlate && request_id_ != 0;

    if (socket_type == ZMQ_ROUTER) {
        msg_t rid;
        if (rid.init_size (routing_id_->size) != 0
            || rid.size () != routing_id_->size) {
            return -1;
        }
        memcpy (rid.data (), routing_id_->data, routing_id_->size);
        int flags = (send_id || part_count_ > 0) ? ZMQ_SNDMORE : 0;
        if (_socket->send (&rid, flags) != 0) {
            return -1;
        }
    }

    if (send_id) {
        msg_t id;
        if (id.init_size (sizeof (uint64_t)) != 0) {
            return -1;
        }
        memcpy (id.data (), &request_id_, sizeof (uint64_t));
        int flags = part_count_ > 0 ? ZMQ_SNDMORE : 0;
        if (_socket->send (&id, flags) != 0) {
            return -1;
        }
    }

    for (size_t i = 0; i < part_count_; ++i) {
        msg_t &part = *reinterpret_cast<msg_t *> (&parts_[i]);
        int flags = (i + 1 < part_count_) ? ZMQ_SNDMORE : 0;
        if (_socket->send (&part, flags) != 0)
            return -1;
    }

    close_msg_array (parts_, part_count_);
    return 0;
}

int zmq::thread_safe_socket_t::send_request (pending_request_t &req_)
{
    if (!_socket) {
        errno = ENOTSOCK;
        return -1;
    }

    const int socket_type = get_socket_type ();
    if (socket_type != ZMQ_ROUTER && socket_type != ZMQ_DEALER) {
        errno = ENOTSUP;
        return -1;
    }
    const bool correlate = req_.correlate;
    const bool send_id = correlate;

    if (socket_type == ZMQ_ROUTER) {
        if (!req_.has_routing_id || req_.routing_id.size == 0) {
            errno = EINVAL;
            return -1;
        }
        msg_t rid;
        if (rid.init_size (req_.routing_id.size) != 0) {
            return -1;
        }
        memcpy (rid.data (), req_.routing_id.data, req_.routing_id.size);
        int flags = (send_id || !req_.parts.empty ()) ? ZMQ_SNDMORE : 0;
        if (_socket->send (&rid, flags) != 0)
            return -1;
    }

    if (send_id) {
        msg_t id;
        if (id.init_size (sizeof (uint64_t)) != 0)
            return -1;
        memcpy (id.data (), &req_.request_id, sizeof (uint64_t));
        int flags = !req_.parts.empty () ? ZMQ_SNDMORE : 0;
        if (_socket->send (&id, flags) != 0)
            return -1;
    }

    for (size_t i = 0; i < req_.parts.size (); ++i) {
        int flags = (i + 1 < req_.parts.size ()) ? ZMQ_SNDMORE : 0;
        if (_socket->send (&req_.parts[i], flags) != 0)
            return -1;
    }

    return 0;
}

int zmq::thread_safe_socket_t::send_request_direct (pending_request_t &req_,
                                                    zmq_msg_t *parts_,
                                                    size_t part_count_,
                                                    int socket_type_)
{
    if (!_socket) {
        errno = ENOTSOCK;
        return -1;
    }

    if (socket_type_ != ZMQ_ROUTER && socket_type_ != ZMQ_DEALER) {
        errno = ENOTSUP;
        return -1;
    }
    const bool send_id = req_.correlate;

    if (socket_type_ == ZMQ_ROUTER) {
        if (!req_.has_routing_id || req_.routing_id.size == 0) {
            errno = EINVAL;
            return -1;
        }
        msg_t rid;
        if (rid.init_size (req_.routing_id.size) != 0)
            return -1;
        memcpy (rid.data (), req_.routing_id.data, req_.routing_id.size);
        int flags = (send_id || part_count_ > 0) ? ZMQ_SNDMORE : 0;
        if (_socket->send (&rid, flags) != 0)
            return -1;
    }

    if (send_id) {
        msg_t id;
        if (id.init_size (sizeof (uint64_t)) != 0)
            return -1;
        memcpy (id.data (), &req_.request_id, sizeof (uint64_t));
        int flags = part_count_ > 0 ? ZMQ_SNDMORE : 0;
        if (_socket->send (&id, flags) != 0)
            return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        msg_t &part = *reinterpret_cast<msg_t *> (&parts_[i]);
        int flags = (i + 1 < part_count_) ? ZMQ_SNDMORE : 0;
        if (_socket->send (&part, flags) != 0)
            return -1;
    }

    return 0;
}

void zmq::thread_safe_socket_t::ensure_request_pump ()
{
    if (!_request_pump_active)
        _request_pump_active = true;
    schedule_request_pump (std::chrono::milliseconds (0));
}

void zmq::thread_safe_socket_t::schedule_request_pump (
  std::chrono::milliseconds delay_)
{
    _request_timer.expires_after (delay_);
    _request_timer.async_wait (boost::asio::bind_executor (
      _strand,
      [this] (const boost::system::error_code &ec_) { pump_requests (ec_); }));
}

void zmq::thread_safe_socket_t::pump_requests (
  const boost::system::error_code &ec_)
{
    if (ec_ == boost::asio::error::operation_aborted)
        return;
    if (!_socket) {
        _request_pump_active = false;
        return;
    }

    incoming_message_t msg;
    while (recv_request_message (&msg))
        handle_incoming_message (&msg);

    process_timeouts ();

    if (!_request_handler && _pending_requests.empty ()) {
        _request_pump_active = false;
        return;
    }

    std::chrono::milliseconds delay (1);
    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now ();
    for (std::unordered_map<uint64_t, pending_request_t>::iterator it =
           _pending_requests.begin ();
         it != _pending_requests.end (); ++it) {
        pending_request_t &req = it->second;
        if (!req.has_deadline)
            continue;
        if (req.deadline <= now) {
            delay = std::chrono::milliseconds (0);
            break;
        }
        const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds> (req.deadline
                                                                 - now);
        if (remaining < delay)
            delay = remaining;
    }

    schedule_request_pump (delay);
}

bool zmq::thread_safe_socket_t::recv_request_message (incoming_message_t *out_)
{
    if (!out_)
        return false;

    msg_t first;
    if (first.init () != 0)
        return false;
    if (_socket->recv (&first, ZMQ_DONTWAIT) != 0) {
        first.close ();
        return false;
    }

    const int socket_type = get_socket_type ();
    const bool correlate = get_request_correlate ();
    out_->parts.clear ();
    out_->has_routing_id = false;
    out_->routing_id.size = 0;
    out_->request_id = 0;

    msg_t current;
    if (current.init () != 0) {
        first.close ();
        return false;
    }
    if (current.move (first) != 0) {
        current.close ();
        return false;
    }

    if (socket_type == ZMQ_ROUTER) {
        out_->has_routing_id = true;
        if (current.size () > sizeof (out_->routing_id.data)) {
            close_parts (&out_->parts);
            current.close ();
            errno = EINVAL;
            return false;
        }
        out_->routing_id.size = static_cast<uint8_t> (current.size ());
        memcpy (out_->routing_id.data, current.data (),
                out_->routing_id.size);

        msg_t next;
        if (next.init () != 0)
            return false;
        if (_socket->recv (&next, 0) != 0) {
            next.close ();
            return false;
        }
        if (current.move (next) != 0)
            return false;
    }

    bool more = current.flags () & msg_t::more;
    if (correlate) {
        if (current.size () == sizeof (uint64_t)) {
            if (!more) {
                // Single-frame payload that is 8 bytes long.
                out_->request_id = 0;
                out_->parts.resize (1);
                out_->parts[0].init ();
                out_->parts[0].move (current);
            } else {
                memcpy (&out_->request_id, current.data (), sizeof (uint64_t));
                current.close ();
            }
        } else {
            out_->request_id = 0;
            out_->parts.resize (1);
            out_->parts[0].init ();
            out_->parts[0].move (current);
        }
    } else {
        out_->request_id = 0;
        out_->parts.resize (1);
        out_->parts[0].init ();
        out_->parts[0].move (current);
    }
    if (!more && out_->parts.empty ()) {
        return true;
    }

    while (more) {
        msg_t part;
        if (part.init () != 0) {
            close_parts (&out_->parts);
            return false;
        }
        if (_socket->recv (&part, 0) != 0) {
            part.close ();
            close_parts (&out_->parts);
            return false;
        }
        more = part.flags () & msg_t::more;
        out_->parts.push_back (msg_t ());
        out_->parts.back ().init ();
        out_->parts.back ().move (part);
    }

    return true;
}

void zmq::thread_safe_socket_t::handle_incoming_message (
  incoming_message_t *msg_)
{
    if (!msg_)
        return;

    if (msg_->request_id != 0) {
        std::unordered_map<uint64_t, pending_request_t>::iterator it =
          _pending_requests.find (msg_->request_id);
        if (it != _pending_requests.end ()) {
            const pending_request_t &stored = it->second;
            bool match = true;
            if (msg_->has_routing_id && msg_->routing_id.size > 0) {
                if (!stored.has_routing_id
                    || stored.routing_id.size != msg_->routing_id.size
                    || memcmp (stored.routing_id.data, msg_->routing_id.data,
                               msg_->routing_id.size)
                         != 0) {
                    match = false;
                }
            }
            if (match) {
                pending_request_t req = std::move (it->second);
                _pending_requests.erase (it);
                handle_group_complete (req);
                handle_request_complete (req, &msg_->parts, 0);
                return;
            }
        }
    } else if (msg_->has_routing_id && msg_->routing_id.size > 0) {
        routing_id_key_t key = make_routing_id_key (msg_->routing_id);
        std::unordered_map<routing_id_key_t,
                           std::deque<uint64_t>,
                           routing_id_key_hash,
                           routing_id_key_equal>::iterator qit =
          _non_correlate_by_rid.find (key);
        if (qit != _non_correlate_by_rid.end ()) {
            while (!qit->second.empty ()) {
                const uint64_t next_id = qit->second.front ();
                qit->second.pop_front ();
                std::unordered_map<uint64_t, pending_request_t>::iterator it =
                  _pending_requests.find (next_id);
                if (it == _pending_requests.end ())
                    continue;
                pending_request_t req = std::move (it->second);
                _pending_requests.erase (it);
                if (qit->second.empty ())
                    _non_correlate_by_rid.erase (qit);
                handle_group_complete (req);
                handle_request_complete (req, &msg_->parts, 0);
                return;
            }
            _non_correlate_by_rid.erase (qit);
        }
    } else if (!_non_correlate_queue.empty ()) {
        while (!_non_correlate_queue.empty ()) {
            const uint64_t next_id = _non_correlate_queue.front ();
            _non_correlate_queue.pop_front ();
            std::unordered_map<uint64_t, pending_request_t>::iterator it =
              _pending_requests.find (next_id);
            if (it == _pending_requests.end ())
                continue;
            pending_request_t req = std::move (it->second);
            _pending_requests.erase (it);
            handle_group_complete (req);
            handle_request_complete (req, &msg_->parts, 0);
            return;
        }
    }

    if (_request_handler) {
        _in_request_handler = true;
        _current_request_id = msg_->request_id;
        if (msg_->has_routing_id)
            _current_routing_id = msg_->routing_id;
        else
            _current_routing_id.size = 0;

        size_t count = 0;
        zmq_msg_t *parts = alloc_msgv_from_parts (&msg_->parts, &count);
        if (parts) {
            _request_handler (parts, count,
                              msg_->has_routing_id ? &msg_->routing_id : NULL,
                              msg_->request_id);
        }

        _in_request_handler = false;
        _current_request_id = 0;
        _current_routing_id.size = 0;
    } else {
        close_parts (&msg_->parts);
    }
}

void zmq::thread_safe_socket_t::handle_request_complete (
  pending_request_t &req_,
  std::vector<msg_t> *parts_,
  int error_)
{
    if (req_.use_callback) {
        zmq_msg_t *out_parts = NULL;
        size_t out_count = 0;
        if (error_ == 0 && parts_ && !parts_->empty ()) {
            out_parts = alloc_msgv_from_parts (parts_, &out_count);
            if (!out_parts) {
                error_ = ENOMEM;
                out_count = 0;
            }
        } else if (parts_) {
            close_parts (parts_);
        }
        if (req_.callback)
            req_.callback (req_.request_id, out_parts, out_count, error_);
    } else {
        completion_entry_t entry;
        entry.request_id = req_.request_id;
        entry.error = error_;
        if (error_ == 0 && parts_)
            entry.parts = std::move (*parts_);
        else if (parts_)
            close_parts (parts_);

        scoped_lock_t lock (_completion_sync);
        _completion_queue.push_back (std::move (entry));
        _completion_cv.broadcast ();
    }

    close_parts (&req_.parts);
}

void zmq::thread_safe_socket_t::handle_group_complete (pending_request_t &req_)
{
    if (req_.group_id == 0)
        return;

    std::unordered_map<uint64_t, group_queue_t>::iterator it =
      _group_queues.find (req_.group_id);
    if (it == _group_queues.end ())
        return;

    group_queue_t &queue = it->second;
    if (queue.inflight_id == req_.request_id) {
        queue.inflight_id = 0;
    } else {
        for (std::deque<uint64_t>::iterator qit = queue.pending.begin ();
             qit != queue.pending.end (); ++qit) {
            if (*qit == req_.request_id) {
                queue.pending.erase (qit);
                break;
            }
        }
    }

    while (queue.inflight_id == 0 && !queue.pending.empty ()) {
        const uint64_t next_id = queue.pending.front ();
        queue.pending.pop_front ();
        std::unordered_map<uint64_t, pending_request_t>::iterator pit =
          _pending_requests.find (next_id);
        if (pit == _pending_requests.end ())
            continue;

        pending_request_t &next_req = pit->second;
        if (send_request (next_req) == 0) {
            queue.inflight_id = next_id;
            if (!next_req.correlate)
                enqueue_non_correlate (next_req);
            close_parts (&next_req.parts);
            break;
        }

        pending_request_t failed_req = std::move (next_req);
        _pending_requests.erase (pit);
        handle_request_complete (failed_req, NULL, errno ? errno : EAGAIN);
    }

    if (queue.inflight_id == 0 && queue.pending.empty ())
        _group_queues.erase (it);
}

void zmq::thread_safe_socket_t::process_timeouts ()
{
    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now ();

    std::vector<pending_request_t> expired;
    for (std::unordered_map<uint64_t, pending_request_t>::iterator it =
           _pending_requests.begin ();
         it != _pending_requests.end ();) {
        pending_request_t &req = it->second;
        if (!req.has_deadline || req.deadline > now) {
            ++it;
            continue;
        }

        expired.push_back (std::move (req));
        it = _pending_requests.erase (it);
    }

    for (size_t i = 0; i < expired.size (); ++i) {
        if (!expired[i].correlate)
            remove_non_correlate (expired[i]);
        handle_group_complete (expired[i]);
        handle_request_complete (expired[i], NULL, ETIMEDOUT);
    }
}

void zmq::thread_safe_socket_t::enqueue_non_correlate (
  const pending_request_t &req_)
{
    if (req_.has_routing_id && req_.routing_id.size > 0) {
        routing_id_key_t key = make_routing_id_key (req_.routing_id);
        _non_correlate_by_rid[key].push_back (req_.request_id);
    } else {
        _non_correlate_queue.push_back (req_.request_id);
    }
}

void zmq::thread_safe_socket_t::remove_non_correlate (
  const pending_request_t &req_)
{
    if (req_.has_routing_id && req_.routing_id.size > 0) {
        routing_id_key_t key = make_routing_id_key (req_.routing_id);
        std::unordered_map<routing_id_key_t,
                           std::deque<uint64_t>,
                           routing_id_key_hash,
                           routing_id_key_equal>::iterator it =
          _non_correlate_by_rid.find (key);
        if (it == _non_correlate_by_rid.end ())
            return;
        for (std::deque<uint64_t>::iterator qit = it->second.begin ();
             qit != it->second.end (); ++qit) {
            if (*qit == req_.request_id) {
                it->second.erase (qit);
                break;
            }
        }
        if (it->second.empty ())
            _non_correlate_by_rid.erase (it);
        return;
    }

    for (std::deque<uint64_t>::iterator it = _non_correlate_queue.begin ();
         it != _non_correlate_queue.end (); ++it) {
        if (*it == req_.request_id) {
            _non_correlate_queue.erase (it);
            break;
        }
    }
}

int zmq::thread_safe_socket_t::cancel_all_requests_internal (int error_)
{
    const int count = static_cast<int> (_pending_requests.size ());
    if (count == 0)
        return 0;

    std::vector<pending_request_t> requests;
    requests.reserve (_pending_requests.size ());
    for (std::unordered_map<uint64_t, pending_request_t>::iterator it =
           _pending_requests.begin ();
         it != _pending_requests.end (); ++it) {
        requests.push_back (std::move (it->second));
    }
    _pending_requests.clear ();
    _group_queues.clear ();
    _non_correlate_queue.clear ();
    _non_correlate_by_rid.clear ();

    for (size_t i = 0; i < requests.size (); ++i) {
        close_parts (&requests[i].parts);
        handle_request_complete (requests[i], NULL, error_);
    }

    return count;
}
