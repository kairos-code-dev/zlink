/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_REQUEST_REPLY_HPP_INCLUDED
#define ZLINK_CPP_REQUEST_REPLY_HPP_INCLUDED

#include "message_socket.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace zlink
{

namespace detail
{

enum class request_reply_message_type_t : uint8_t
{
    data = 0,
    request = 1,
    reply = 2
};

inline error_t make_error_from_errno (int code_)
{
    return error_t (code_ > 0 ? code_ : zlink_errno ());
}

inline void set_promise_exception (std::promise<received_t> &promise_,
                                   int code_)
{
    try {
        throw make_error_from_errno (code_);
    } catch (...) {
        promise_.set_exception (std::current_exception ());
    }
}

struct request_reply_pending_t
{
    std::promise<received_t> promise;
    std::function<void(received_t)> on_reply;
    std::function<void(error_t)> on_error;
    std::atomic<bool> completed;

    request_reply_pending_t () : completed (false) {}

    bool mark_completed () { return !completed.exchange (true); }
};

template<typename SocketT> class request_reply_base_t
{
  public:
    explicit request_reply_base_t (SocketT &socket_)
        : _socket (socket_),
          _default_timeout (std::chrono::milliseconds (30000)),
          _started (false),
          _stop (false),
          _callback_mode (false)
    {
    }

    request_reply_base_t (const request_reply_base_t &) = delete;
    request_reply_base_t &operator= (const request_reply_base_t &) = delete;

    virtual ~request_reply_base_t () { shutdown (); }

    void set_default_request_timeout (std::chrono::milliseconds timeout_)
    {
        std::lock_guard<std::mutex> lock (_state_mutex);
        _default_timeout = timeout_;
    }

    ZLINK_CPP_NODISCARD std::chrono::milliseconds
    get_default_request_timeout () const
    {
        std::lock_guard<std::mutex> lock (_state_mutex);
        return _default_timeout;
    }

    ZLINK_CPP_NODISCARD received_t recv ()
    {
        ensure_dispatch_started ();
        std::unique_lock<std::mutex> lock (_state_mutex);
        if (_callback_mode)
            throw std::logic_error ("socket is in callback mode");
        _data_cv.wait (lock, [this] {
            return _stop || !_data_queue.empty ();
        });
        if (_data_queue.empty ())
            throw make_error_from_errno (ETERM);
        received_t received = std::move (_data_queue.front ());
        _data_queue.pop_front ();
        return received;
    }

    ZLINK_CPP_NODISCARD maybe_t<received_t> try_recv ()
    {
        ensure_dispatch_started ();
        std::lock_guard<std::mutex> lock (_state_mutex);
        if (_callback_mode)
            throw std::logic_error ("socket is in callback mode");
        if (_data_queue.empty ())
            return maybe_t<received_t> ();
        received_t received = std::move (_data_queue.front ());
        _data_queue.pop_front ();
        return maybe_t<received_t> (std::move (received));
    }

    void
    on_receive (std::function<void(received_t)> handler_)
    {
        ensure_dispatch_started ();
        std::lock_guard<std::mutex> lock (_state_mutex);
        _data_handler = handler_;
        _callback_mode = static_cast<bool> (_data_handler);
    }

  protected:
    SocketT &_socket;

    void ensure_dispatch_started ()
    {
        bool expected = false;
        if (_started.compare_exchange_strong (expected, true)) {
            _dispatch_thread =
              std::thread (&request_reply_base_t::dispatch_loop, this);
        }
    }

    void shutdown ()
    {
        if (!_started.load ())
            return;
        _stop.store (true);
        _data_cv.notify_all ();
        _shutdown_cv.notify_all ();
        fail_all_pending (ETERM);
        if (_dispatch_thread.joinable ())
            _dispatch_thread.join ();
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> lock (_worker_mutex);
            workers.swap (_workers);
        }
        for (size_t i = 0; i < workers.size (); ++i) {
            if (workers[i].joinable ())
                workers[i].join ();
        }
    }

    ZLINK_CPP_NODISCARD std::future<received_t>
    make_pending_future (uint64_t correlation_id_,
                         std::function<void(received_t)> on_reply_,
                         std::function<void(error_t)> on_error_,
                         std::chrono::milliseconds timeout_)
    {
        std::shared_ptr<request_reply_pending_t> pending (
          new request_reply_pending_t ());
        pending->on_reply = on_reply_;
        pending->on_error = on_error_;
        std::future<received_t> future = pending->promise.get_future ();

        {
            std::lock_guard<std::mutex> lock (_state_mutex);
            _pending[correlation_id_] = pending;
        }

        launch_worker ([this, correlation_id_, pending, timeout_] {
            std::unique_lock<std::mutex> shutdown_lock (_worker_mutex);
            if (_shutdown_cv.wait_for (
                  shutdown_lock, timeout_, [this] { return _stop.load (); }))
                return;
            shutdown_lock.unlock ();
            if (!pending->mark_completed ())
                return;
            {
                std::lock_guard<std::mutex> lock (_state_mutex);
                _pending.erase (correlation_id_);
            }
            if (pending->on_error)
                pending->on_error (error_t (ETIMEDOUT));
            set_promise_exception (pending->promise, ETIMEDOUT);
        });

        return future;
    }

    void resolve_reply (uint64_t correlation_id_, received_t received_)
    {
        std::shared_ptr<request_reply_pending_t> pending;
        {
            std::lock_guard<std::mutex> lock (_state_mutex);
            typename std::unordered_map<uint64_t,
                                        std::shared_ptr<request_reply_pending_t> >::iterator
              it = _pending.find (correlation_id_);
            if (it == _pending.end ())
                return;
            pending = it->second;
            _pending.erase (it);
        }

        if (!pending->mark_completed ())
            return;

        if (pending->on_reply)
            pending->on_reply (received_);
        pending->promise.set_value (std::move (received_));
    }

    void fail_pending (uint64_t correlation_id_, int code_)
    {
        std::shared_ptr<request_reply_pending_t> pending;
        {
            std::lock_guard<std::mutex> lock (_state_mutex);
            typename std::unordered_map<uint64_t,
                                        std::shared_ptr<request_reply_pending_t> >::iterator
              it = _pending.find (correlation_id_);
            if (it == _pending.end ())
                return;
            pending = it->second;
            _pending.erase (it);
        }

        if (!pending->mark_completed ())
            return;

        if (pending->on_error)
            pending->on_error (make_error_from_errno (code_));
        set_promise_exception (pending->promise, code_);
    }

    void fail_all_pending (int code_)
    {
        std::unordered_map<uint64_t, std::shared_ptr<request_reply_pending_t> > pending;
        {
            std::lock_guard<std::mutex> lock (_state_mutex);
            pending.swap (_pending);
        }

        for (typename std::unordered_map<uint64_t,
                                         std::shared_ptr<request_reply_pending_t> >::iterator
               it = pending.begin ();
             it != pending.end (); ++it) {
            if (!it->second->mark_completed ())
                continue;
            if (it->second->on_error)
                it->second->on_error (make_error_from_errno (code_));
            set_promise_exception (it->second->promise, code_);
        }
    }

    void deliver_data (received_t received_)
    {
        std::function<void(received_t)> handler;
        {
            std::lock_guard<std::mutex> lock (_state_mutex);
            if (_data_handler) {
                handler = _data_handler;
            } else {
                _data_queue.push_back (std::move (received_));
                _data_cv.notify_one ();
                return;
            }
        }
        handler (std::move (received_));
    }

    void launch_worker (std::function<void()> fn_)
    {
        std::lock_guard<std::mutex> lock (_worker_mutex);
        _workers.push_back (std::thread ([fn = std::move (fn_)] () mutable {
            fn ();
        }));
    }

    virtual void handle_dispatch (received_t received_) = 0;

  private:
    void dispatch_loop ()
    {
        while (!_stop.load ()) {
            try {
                maybe_t<received_t> maybe_received = _socket.try_recv ();
                if (!maybe_received) {
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                    continue;
                }
                handle_dispatch (std::move (*maybe_received));
            } catch (const error_t &error) {
                _stop.store (true);
                _data_cv.notify_all ();
                _shutdown_cv.notify_all ();
                fail_all_pending (error.code ());
            } catch (...) {
                _stop.store (true);
                _data_cv.notify_all ();
                _shutdown_cv.notify_all ();
                fail_all_pending (zlink_errno ());
            }
        }
    }

    mutable std::mutex _state_mutex;
    std::chrono::milliseconds _default_timeout;
    std::unordered_map<uint64_t, std::shared_ptr<request_reply_pending_t> > _pending;
    std::deque<received_t> _data_queue;
    std::function<void(received_t)> _data_handler;
    std::condition_variable _data_cv;
    std::condition_variable _shutdown_cv;
    std::atomic<bool> _started;
    std::atomic<bool> _stop;
    bool _callback_mode;
    std::thread _dispatch_thread;
    std::mutex _worker_mutex;
    std::vector<std::thread> _workers;
};

inline bool read_request_info (const received_t &received_,
                               uint8_t &msg_type_out_,
                               uint64_t &correlation_id_out_)
{
    if (received_.parts.empty ())
        return false;
    return received_.parts[0].get_request_info (
             &msg_type_out_, &correlation_id_out_)
           == 0;
}

} // namespace detail

class request_dealer_t : public detail::request_reply_base_t<dealer_socket_t>
{
  public:
    explicit request_dealer_t (dealer_socket_t &socket_)
        : detail::request_reply_base_t<dealer_socket_t> (socket_),
          _next_correlation_id (1)
    {
    }

    ZLINK_CPP_NODISCARD std::future<received_t> request (message_t message_)
    {
        return request (std::move (message_), get_default_request_timeout ());
    }

    ZLINK_CPP_NODISCARD std::future<received_t>
    request (message_t message_, std::chrono::milliseconds timeout_)
    {
        ensure_dispatch_started ();
        const uint64_t correlation_id = next_correlation_id ();
        if (message_.set_request (correlation_id) != 0)
            throw detail::make_error_from_errno (zlink_errno ());

        std::future<received_t> future = make_pending_future (
          correlation_id, std::function<void(received_t)> (),
          std::function<void(error_t)> (), timeout_);

        launch_worker ([this, correlation_id, message = std::move (message_)] () mutable {
            try {
                _socket.send (message);
            } catch (const error_t &error) {
                fail_pending (correlation_id, error.code ());
            } catch (...) {
                fail_pending (correlation_id, zlink_errno ());
            }
        });

        return future;
    }

    void request (message_t message_,
                  std::function<void(received_t)> on_reply_,
                  std::function<void(error_t)> on_error_)
    {
        request (std::move (message_), on_reply_, on_error_,
                 get_default_request_timeout ());
    }

    void request (message_t message_,
                  std::function<void(received_t)> on_reply_,
                  std::function<void(error_t)> on_error_,
                  std::chrono::milliseconds timeout_)
    {
        ensure_dispatch_started ();
        const uint64_t correlation_id = next_correlation_id ();
        if (message_.set_request (correlation_id) != 0) {
            on_error_ (detail::make_error_from_errno (zlink_errno ()));
            return;
        }

        (void) make_pending_future (
          correlation_id, on_reply_, on_error_, timeout_);

        launch_worker ([this, correlation_id, message = std::move (message_)] () mutable {
            try {
                _socket.send (message);
            } catch (const error_t &error) {
                fail_pending (correlation_id, error.code ());
            } catch (...) {
                fail_pending (correlation_id, zlink_errno ());
            }
        });
    }

    ZLINK_CPP_NODISCARD std::future<received_t> try_request (message_t message_)
    {
        return try_request (std::move (message_), get_default_request_timeout ());
    }

    ZLINK_CPP_NODISCARD std::future<received_t>
    try_request (message_t message_, std::chrono::milliseconds timeout_)
    {
        ensure_dispatch_started ();
        const uint64_t correlation_id = next_correlation_id ();
        if (message_.set_request (correlation_id) != 0)
            throw detail::make_error_from_errno (zlink_errno ());

        std::future<received_t> future = make_pending_future (
          correlation_id, std::function<void(received_t)> (),
          std::function<void(error_t)> (), timeout_);

        try {
            const send_result_t result = _socket.try_send (message_);
            if (result != send_result_t::sent) {
                fail_pending (correlation_id, EAGAIN);
                return future;
            }
        } catch (const error_t &error) {
            fail_pending (correlation_id, error.code ());
        } catch (...) {
            fail_pending (correlation_id, zlink_errno ());
        }

        return future;
    }

    void try_request (message_t message_,
                      std::function<void(received_t)> on_reply_,
                      std::function<void(error_t)> on_error_)
    {
        try_request (std::move (message_), on_reply_, on_error_,
                     get_default_request_timeout ());
    }

    void try_request (message_t message_,
                      std::function<void(received_t)> on_reply_,
                      std::function<void(error_t)> on_error_,
                      std::chrono::milliseconds timeout_)
    {
        ensure_dispatch_started ();
        const uint64_t correlation_id = next_correlation_id ();
        if (message_.set_request (correlation_id) != 0) {
            on_error_ (detail::make_error_from_errno (zlink_errno ()));
            return;
        }

        (void) make_pending_future (
          correlation_id, on_reply_, on_error_, timeout_);

        try {
            const send_result_t result = _socket.try_send (message_);
            if (result != send_result_t::sent)
                fail_pending (correlation_id, EAGAIN);
        } catch (const error_t &error) {
            fail_pending (correlation_id, error.code ());
        } catch (...) {
            fail_pending (correlation_id, zlink_errno ());
        }
    }

  protected:
    void handle_dispatch (received_t received_) override
    {
        uint8_t msg_type = static_cast<uint8_t> (
          detail::request_reply_message_type_t::data);
        uint64_t correlation_id = 0;
        if (!detail::read_request_info (received_, msg_type, correlation_id)) {
            deliver_data (std::move (received_));
            return;
        }

        if (msg_type == static_cast<uint8_t> (
                          detail::request_reply_message_type_t::reply)) {
            resolve_reply (correlation_id, std::move (received_));
            return;
        }

        deliver_data (std::move (received_));
    }

  private:
    uint64_t next_correlation_id ()
    {
        return _next_correlation_id.fetch_add (1);
    }

    std::atomic<uint64_t> _next_correlation_id;
};

class request_router_t : public detail::request_reply_base_t<router_socket_t>
{
  public:
    explicit request_router_t (router_socket_t &socket_)
        : detail::request_reply_base_t<router_socket_t> (socket_),
          _next_correlation_id (1)
    {
    }

    ZLINK_CPP_NODISCARD std::future<received_t>
    request (const routing_id_t &routing_id_, message_t message_)
    {
        return request (
          routing_id_, std::move (message_), get_default_request_timeout ());
    }

    ZLINK_CPP_NODISCARD std::future<received_t>
    request (const routing_id_t &routing_id_,
             message_t message_,
             std::chrono::milliseconds timeout_)
    {
        ensure_dispatch_started ();
        const uint64_t correlation_id = next_correlation_id ();
        if (message_.set_request (correlation_id) != 0)
            throw detail::make_error_from_errno (zlink_errno ());

        std::future<received_t> future = make_pending_future (
          correlation_id, std::function<void(received_t)> (),
          std::function<void(error_t)> (), timeout_);

        launch_worker ([this, correlation_id, routing_id_, message = std::move (message_)] () mutable {
            try {
                _socket.send (routing_id_, message);
            } catch (const error_t &error) {
                fail_pending (correlation_id, error.code ());
            } catch (...) {
                fail_pending (correlation_id, zlink_errno ());
            }
        });

        return future;
    }

    void request (const routing_id_t &routing_id_,
                  message_t message_,
                  std::function<void(received_t)> on_reply_,
                  std::function<void(error_t)> on_error_)
    {
        request (routing_id_, std::move (message_), on_reply_, on_error_,
                 get_default_request_timeout ());
    }

    void request (const routing_id_t &routing_id_,
                  message_t message_,
                  std::function<void(received_t)> on_reply_,
                  std::function<void(error_t)> on_error_,
                  std::chrono::milliseconds timeout_)
    {
        ensure_dispatch_started ();
        const uint64_t correlation_id = next_correlation_id ();
        if (message_.set_request (correlation_id) != 0) {
            on_error_ (detail::make_error_from_errno (zlink_errno ()));
            return;
        }

        (void) make_pending_future (
          correlation_id, on_reply_, on_error_, timeout_);

        launch_worker ([this, correlation_id, routing_id_, message = std::move (message_)] () mutable {
            try {
                _socket.send (routing_id_, message);
            } catch (const error_t &error) {
                fail_pending (correlation_id, error.code ());
            } catch (...) {
                fail_pending (correlation_id, zlink_errno ());
            }
        });
    }

    ZLINK_CPP_NODISCARD std::future<received_t>
    try_request (const routing_id_t &routing_id_, message_t message_)
    {
        return try_request (
          routing_id_, std::move (message_), get_default_request_timeout ());
    }

    ZLINK_CPP_NODISCARD std::future<received_t>
    try_request (const routing_id_t &routing_id_,
                 message_t message_,
                 std::chrono::milliseconds timeout_)
    {
        ensure_dispatch_started ();
        const uint64_t correlation_id = next_correlation_id ();
        if (message_.set_request (correlation_id) != 0)
            throw detail::make_error_from_errno (zlink_errno ());

        std::future<received_t> future = make_pending_future (
          correlation_id, std::function<void(received_t)> (),
          std::function<void(error_t)> (), timeout_);

        try {
            const send_result_t result = _socket.try_send (routing_id_, message_);
            if (result != send_result_t::sent)
                fail_pending (correlation_id, EAGAIN);
        } catch (const error_t &error) {
            fail_pending (correlation_id, error.code ());
        } catch (...) {
            fail_pending (correlation_id, zlink_errno ());
        }

        return future;
    }

    void try_request (const routing_id_t &routing_id_,
                      message_t message_,
                      std::function<void(received_t)> on_reply_,
                      std::function<void(error_t)> on_error_)
    {
        try_request (routing_id_, std::move (message_), on_reply_, on_error_,
                     get_default_request_timeout ());
    }

    void try_request (const routing_id_t &routing_id_,
                      message_t message_,
                      std::function<void(received_t)> on_reply_,
                      std::function<void(error_t)> on_error_,
                      std::chrono::milliseconds timeout_)
    {
        ensure_dispatch_started ();
        const uint64_t correlation_id = next_correlation_id ();
        if (message_.set_request (correlation_id) != 0) {
            on_error_ (detail::make_error_from_errno (zlink_errno ()));
            return;
        }

        (void) make_pending_future (
          correlation_id, on_reply_, on_error_, timeout_);

        try {
            const send_result_t result = _socket.try_send (routing_id_, message_);
            if (result != send_result_t::sent)
                fail_pending (correlation_id, EAGAIN);
        } catch (const error_t &error) {
            fail_pending (correlation_id, error.code ());
        } catch (...) {
            fail_pending (correlation_id, zlink_errno ());
        }
    }

    void reply (const routing_id_t &routing_id_,
                uint64_t correlation_id_,
                message_t message_)
    {
        if (message_.set_reply (correlation_id_) != 0)
            throw detail::make_error_from_errno (zlink_errno ());
        launch_worker ([this, routing_id_, message = std::move (message_)] () mutable {
            try {
                for (;;) {
                    send_result_t result = _socket.try_send (routing_id_, message);
                    if (result == send_result_t::sent)
                        return;
                    if (result != send_result_t::backpressured)
                        return;
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                }
            } catch (...) {
            }
        });
    }

    ZLINK_CPP_NODISCARD send_result_t
    try_reply (const routing_id_t &routing_id_,
               uint64_t correlation_id_,
               message_t message_)
    {
        if (message_.set_reply (correlation_id_) != 0)
            throw detail::make_error_from_errno (zlink_errno ());
        return _socket.try_send (routing_id_, message_);
    }

    void on_request (
      std::function<void(routing_id_t, uint64_t, received_t)> handler_)
    {
        ensure_dispatch_started ();
        std::lock_guard<std::mutex> lock (_request_mutex);
        _request_handler = handler_;
    }

  protected:
    void handle_dispatch (received_t received_) override
    {
        uint8_t msg_type = static_cast<uint8_t> (
          detail::request_reply_message_type_t::data);
        uint64_t correlation_id = 0;
        if (!detail::read_request_info (received_, msg_type, correlation_id)) {
            deliver_data (std::move (received_));
            return;
        }

        if (msg_type == static_cast<uint8_t> (
                          detail::request_reply_message_type_t::reply)) {
            resolve_reply (correlation_id, std::move (received_));
            return;
        }

        if (msg_type == static_cast<uint8_t> (
                          detail::request_reply_message_type_t::request)) {
            std::function<void(routing_id_t, uint64_t, received_t)> handler;
            {
                std::lock_guard<std::mutex> lock (_request_mutex);
                handler = _request_handler;
            }
            if (handler)
                handler (received_.routing_id, correlation_id,
                         std::move (received_));
            else
                deliver_data (std::move (received_));
            return;
        }

        deliver_data (std::move (received_));
    }

  private:
    uint64_t next_correlation_id ()
    {
        return _next_correlation_id.fetch_add (1);
    }

    std::atomic<uint64_t> _next_correlation_id;
    std::mutex _request_mutex;
    std::function<void(routing_id_t, uint64_t, received_t)> _request_handler;
};

} // namespace zlink

#endif
