/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_REQUEST_REPLY_HPP_INCLUDED
#define ZLINK_CPP_REQUEST_REPLY_HPP_INCLUDED

#include "message_socket.hpp"

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>

namespace zlink
{

template<typename T> class async_result_t
{
  public:
    explicit async_result_t (std::future<T> future_) : _future (std::move (future_)) {}

    bool valid () const { return _future.valid (); }
    void wait () const { _future.wait (); }
    template<typename Rep, typename Period>
    std::future_status wait_for (const std::chrono::duration<Rep, Period> &timeout_) const
    {
        return _future.wait_for (timeout_);
    }
    T get () { return _future.get (); }

  private:
    mutable std::future<T> _future;
};

namespace detail
{

struct cpp_reply_state_t
{
    std::promise<received_t> promise;
    std::function<void(received_t)> on_reply;
    std::function<void(error_t)> on_error;
};

inline received_t take_received (const zlink_routing_id_t *rid_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 uint64_t request_seq_ = 0,
                                 bool has_request_seq_ = false)
{
    received_t received;
    if (rid_)
        received.routing_id = routing_id_t (*rid_);
    std::vector<message_t> parts;
    parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i)
        (void) zlink_msg_move (parts[i].handle (), &parts_[i]);
    received.parts.swap (parts);
    received.request_seq = request_seq_;
    received.has_request_seq = has_request_seq_;
    return received;
}

inline received_t recv_router_received (void *router_handle_, int flags_)
{
    const zlink_routing_id_t *peer_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = zlink_router_recv (router_handle_, &peer_rid, &request_seq,
                                      &parts, &part_count, flags_);
    if (rc != 0)
        throw last_error ();
    return take_received (peer_rid, parts, part_count, request_seq,
                          request_seq != 0);
}

inline void complete_reply_state (cpp_reply_state_t *state_,
                                  int errnum_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_)
{
    if (!state_)
        return;
    std::unique_ptr<cpp_reply_state_t> holder (state_);
    if (errnum_ != 0) {
        if (holder->on_error)
            holder->on_error (error_t (errnum_));
        holder->promise.set_exception (
          std::make_exception_ptr (error_t (errnum_)));
        return;
    }
    received_t received = take_received (NULL, parts_, part_count_);
    if (holder->on_reply)
        holder->on_reply (received);
    holder->promise.set_value (std::move (received));
}

inline void reply_callback (int errno_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            void *userdata_)
{
    complete_reply_state (
      static_cast<cpp_reply_state_t *> (userdata_), errno_, parts_, part_count_);
}

template<typename SocketT> inline int
prepare_native_parts (message_t &message_, std::vector<zlink_msg_t> &native_)
{
    std::vector<message_t> messages;
    messages.push_back (std::move (message_));
    return detail::move_parts_to_native (messages, native_);
}

} // namespace detail

class request_dealer_t
{
  public:
    explicit request_dealer_t (dealer_socket_t &socket_)
        : _socket (socket_), _default_timeout (std::chrono::milliseconds (5000))
    {
    }

    void set_default_request_timeout (std::chrono::milliseconds timeout_)
    {
        _default_timeout = timeout_;
    }

    std::chrono::milliseconds get_default_request_timeout () const
    {
        return _default_timeout;
    }

    async_result_t<received_t> request (message_t message_)
    {
        return request (std::move (message_), _default_timeout);
    }

    async_result_t<received_t> request (message_t message_,
                                        std::chrono::milliseconds timeout_)
    {
        detail::cpp_reply_state_t *state = new detail::cpp_reply_state_t ();
        async_result_t<received_t> result (state->promise.get_future ());
        std::vector<zlink_msg_t> native;
        if (detail::prepare_native_parts<dealer_socket_t> (message_, native) != 0) {
            delete state;
            throw last_error ();
        }
        const int rc = zlink_dealer_request (_socket.handle (), native.data (),
                                             native.size (),
                                             static_cast<uint32_t> (timeout_.count ()),
                                             &detail::reply_callback, state);
        if (rc != 0) {
            delete state;
            throw last_error ();
        }
        return result;
    }

    void request (message_t message_,
                  std::function<void(received_t)> on_reply_,
                  std::function<void(error_t)> on_error_,
                  std::chrono::milliseconds timeout_)
    {
        detail::cpp_reply_state_t *state = new detail::cpp_reply_state_t ();
        state->on_reply = on_reply_;
        state->on_error = on_error_;
        std::vector<zlink_msg_t> native;
        if (detail::prepare_native_parts<dealer_socket_t> (message_, native) != 0) {
            delete state;
            throw last_error ();
        }
        const int rc = zlink_dealer_request (_socket.handle (), native.data (),
                                             native.size (),
                                             static_cast<uint32_t> (timeout_.count ()),
                                             &detail::reply_callback, state);
        if (rc != 0) {
            delete state;
            throw last_error ();
        }
    }

    void request (message_t message_,
                  std::function<void(received_t)> on_reply_,
                  std::function<void(error_t)> on_error_)
    {
        request (std::move (message_), on_reply_, on_error_, _default_timeout);
    }

    async_result_t<received_t> try_request (message_t message_)
    {
        return request (std::move (message_), _default_timeout);
    }

    async_result_t<received_t> try_request (message_t message_,
                                            std::chrono::milliseconds timeout_)
    {
        return request (std::move (message_), timeout_);
    }

    void try_request (message_t message_,
                      std::function<void(received_t)> on_reply_,
                      std::function<void(error_t)> on_error_,
                      std::chrono::milliseconds timeout_)
    {
        request (std::move (message_), on_reply_, on_error_, timeout_);
    }

    void try_request (message_t message_,
                      std::function<void(received_t)> on_reply_,
                      std::function<void(error_t)> on_error_)
    {
        request (std::move (message_), on_reply_, on_error_, _default_timeout);
    }

    received_t recv () { return _socket.recv (); }

    maybe_t<received_t> try_recv () { return _socket.try_recv (); }

    int on_receive (zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL)
    {
        return _socket.on_receive (handler_, userdata_);
    }

  private:
    dealer_socket_t &_socket;
    std::chrono::milliseconds _default_timeout;
};

class request_router_t
{
  public:
    explicit request_router_t (router_socket_t &socket_)
        : _socket (socket_),
          _default_timeout (std::chrono::milliseconds (5000)),
          _callback_mode (false)
    {
        const int rc =
          zlink_router_handler (_socket.handle (), &request_callback, this);
        if (rc != 0)
            throw last_error ();
    }

    void set_default_request_timeout (std::chrono::milliseconds timeout_)
    {
        _default_timeout = timeout_;
    }

    std::chrono::milliseconds get_default_request_timeout () const
    {
        return _default_timeout;
    }

    async_result_t<received_t> request (const routing_id_t &routing_id_,
                                        message_t message_)
    {
        return request (routing_id_, std::move (message_), _default_timeout);
    }

    async_result_t<received_t> request (const routing_id_t &routing_id_,
                                        message_t message_,
                                        std::chrono::milliseconds timeout_)
    {
        detail::cpp_reply_state_t *state = new detail::cpp_reply_state_t ();
        async_result_t<received_t> result (state->promise.get_future ());
        std::vector<zlink_msg_t> native;
        if (detail::prepare_native_parts<router_socket_t> (message_, native) != 0) {
            delete state;
            throw last_error ();
        }
        const int rc = zlink_router_request (_socket.handle (),
                                             routing_id_native (routing_id_),
                                             native.data (), native.size (),
                                             static_cast<uint32_t> (timeout_.count ()),
                                             &detail::reply_callback, state);
        if (rc != 0) {
            delete state;
            throw last_error ();
        }
        return result;
    }

    void request (const routing_id_t &routing_id_,
                  message_t message_,
                  std::function<void(received_t)> on_reply_,
                  std::function<void(error_t)> on_error_,
                  std::chrono::milliseconds timeout_)
    {
        detail::cpp_reply_state_t *state = new detail::cpp_reply_state_t ();
        state->on_reply = on_reply_;
        state->on_error = on_error_;
        std::vector<zlink_msg_t> native;
        if (detail::prepare_native_parts<router_socket_t> (message_, native) != 0) {
            delete state;
            throw last_error ();
        }
        const int rc = zlink_router_request (_socket.handle (),
                                             routing_id_native (routing_id_),
                                             native.data (), native.size (),
                                             static_cast<uint32_t> (timeout_.count ()),
                                             &detail::reply_callback, state);
        if (rc != 0) {
            delete state;
            throw last_error ();
        }
    }

    void request (const routing_id_t &routing_id_,
                  message_t message_,
                  std::function<void(received_t)> on_reply_,
                  std::function<void(error_t)> on_error_)
    {
        request (routing_id_, std::move (message_), on_reply_, on_error_,
                 _default_timeout);
    }

    async_result_t<received_t> try_request (const routing_id_t &routing_id_,
                                            message_t message_)
    {
        return request (routing_id_, std::move (message_), _default_timeout);
    }

    async_result_t<received_t> try_request (const routing_id_t &routing_id_,
                                            message_t message_,
                                            std::chrono::milliseconds timeout_)
    {
        return request (routing_id_, std::move (message_), timeout_);
    }

    void try_request (const routing_id_t &routing_id_,
                      message_t message_,
                      std::function<void(received_t)> on_reply_,
                      std::function<void(error_t)> on_error_,
                      std::chrono::milliseconds timeout_)
    {
        request (routing_id_, std::move (message_), on_reply_, on_error_, timeout_);
    }

    void try_request (const routing_id_t &routing_id_,
                      message_t message_,
                      std::function<void(received_t)> on_reply_,
                      std::function<void(error_t)> on_error_)
    {
        request (routing_id_, std::move (message_), on_reply_, on_error_,
                 _default_timeout);
    }

    void reply (const routing_id_t &routing_id_,
                uint64_t request_seq_,
                message_t message_)
    {
        std::vector<zlink_msg_t> native;
        if (detail::prepare_native_parts<router_socket_t> (message_, native) != 0)
            throw last_error ();
        const int rc = zlink_router_reply (_socket.handle (),
                                           routing_id_native (routing_id_),
                                           request_seq_, native.data (),
                                           native.size ());
        if (rc != 0)
            throw last_error ();
    }

    send_result_t try_reply (const routing_id_t &routing_id_,
                             uint64_t request_seq_,
                             message_t message_)
    {
        reply (routing_id_, request_seq_, std::move (message_));
        return send_result_t::sent;
    }

    received_t recv ()
    {
        {
            std::unique_lock<std::mutex> lock (_mutex);
            if (_callback_mode)
                throw std::logic_error ("socket is in callback mode");
            if (!_queue.empty ()) {
                received_t received = std::move (_queue.front ());
                _queue.pop ();
                return received;
            }
        }
        return detail::recv_router_received (_socket.handle (), 0);
    }

    maybe_t<received_t> try_recv ()
    {
        std::lock_guard<std::mutex> lock (_mutex);
        if (_callback_mode)
            throw std::logic_error ("socket is in callback mode");
        if (_queue.empty ()) {
            try {
                return maybe_t<received_t> (
                  detail::recv_router_received (_socket.handle (), ZLINK_DONTWAIT));
            }
            catch (const error_t &err) {
                if (err.num () == EAGAIN)
                    return maybe_t<received_t> ();
                throw;
            }
        }
        received_t received = std::move (_queue.front ());
        _queue.pop ();
        return maybe_t<received_t> (std::move (received));
    }

    void on_receive (std::function<void(received_t)> handler_)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _callback = handler_;
        _callback_mode = static_cast<bool> (_callback);
    }

  private:
    static void request_callback (const zlink_routing_id_t *peer_rid_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *userdata_)
    {
        request_router_t *self = static_cast<request_router_t *> (userdata_);
        if (!self)
            return;
        received_t received =
          detail::take_received (peer_rid_, parts_, part_count_, request_seq_, true);
        std::function<void(received_t)> callback;
        {
            std::lock_guard<std::mutex> lock (self->_mutex);
            callback = self->_callback;
            if (!callback) {
                self->_queue.push (std::move (received));
                self->_cv.notify_one ();
                return;
            }
        }
        callback (std::move (received));
    }

    router_socket_t &_socket;
    std::chrono::milliseconds _default_timeout;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::queue<received_t> _queue;
    std::function<void(received_t)> _callback;
    bool _callback_mode;
};

} // namespace zlink

#endif
