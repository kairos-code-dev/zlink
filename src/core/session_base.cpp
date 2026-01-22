/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "utils/macros.hpp"
#include "core/session_base.hpp"
#include "engine/i_engine.hpp"
#include "utils/err.hpp"
#include "core/pipe.hpp"
#include "utils/likely.hpp"
#include "core/address.hpp"

// ASIO-only build: Transport connecters are always included
#include "transports/tcp/asio_tcp_connecter.hpp"
#if defined ZMQ_HAVE_IPC
#include "transports/ipc/asio_ipc_connecter.hpp"
#endif
#if defined ZMQ_HAVE_ASIO_SSL
#include "transports/tls/asio_tls_connecter.hpp"
#endif
#if defined ZMQ_HAVE_WS
#include "transports/ws/asio_ws_connecter.hpp"
#endif

#include "core/ctx.hpp"

zmq::session_base_t *zmq::session_base_t::create (class io_thread_t *io_thread_,
                                                  bool active_,
                                                  class socket_base_t *socket_,
                                                  const options_t &options_,
                                                  address_t *addr_)
{
    session_base_t *s = NULL;
    switch (options_.type) {
        case ZMQ_DEALER:
        case ZMQ_ROUTER:
        case ZMQ_PUB:
        case ZMQ_XPUB:
        case ZMQ_SUB:
        case ZMQ_XSUB:
        case ZMQ_PAIR:
            s = new (std::nothrow)
              session_base_t (io_thread_, active_, socket_, options_, addr_);
            break;

        default:
            errno = EINVAL;
            return NULL;
    }
    alloc_assert (s);
    return s;
}

zmq::session_base_t::session_base_t (class io_thread_t *io_thread_,
                                     bool active_,
                                     class socket_base_t *socket_,
                                     const options_t &options_,
                                     address_t *addr_) :
    own_t (io_thread_, options_),
    io_object_t (io_thread_),
    _active (active_),
    _pipe (NULL),
    _incomplete_in (false),
    _pending (false),
    _engine (NULL),
    _socket (socket_),
    _io_thread (io_thread_),
    _has_linger_timer (false),
    _addr (addr_)
{
}

const zmq::endpoint_uri_pair_t &zmq::session_base_t::get_endpoint () const
{
    return _engine->get_endpoint ();
}

zmq::session_base_t::~session_base_t ()
{
    zmq_assert (!_pipe);

    //  If there's still a pending linger timer, remove it.
    if (_has_linger_timer) {
        cancel_timer (linger_timer_id);
        _has_linger_timer = false;
    }

    //  Close the engine.
    if (_engine)
        _engine->terminate ();

    LIBZMQ_DELETE (_addr);
}

void zmq::session_base_t::attach_pipe (pipe_t *pipe_)
{
    zmq_assert (!is_terminating ());
    zmq_assert (!_pipe);
    zmq_assert (pipe_);
    _pipe = pipe_;
    _pipe->set_event_sink (this);
}

int zmq::session_base_t::pull_msg (msg_t *msg_)
{
    if (!_pipe || !_pipe->read (msg_)) {
        errno = EAGAIN;
        return -1;
    }

    _incomplete_in = (msg_->flags () & msg_t::more) != 0;

    return 0;
}

int zmq::session_base_t::push_msg (msg_t *msg_)
{
    //  pass subscribe/cancel to the sockets
    if ((msg_->flags () & msg_t::command) && !msg_->is_subscribe ()
        && !msg_->is_cancel ())
        return 0;
    if (_pipe && _pipe->write (msg_)) {
        const int rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    errno = EAGAIN;
    return -1;
}

void zmq::session_base_t::reset ()
{
}

void zmq::session_base_t::flush ()
{
    if (_pipe)
        _pipe->flush ();
}

void zmq::session_base_t::rollback ()
{
    if (_pipe)
        _pipe->rollback ();
}

void zmq::session_base_t::clean_pipes ()
{
    zmq_assert (_pipe != NULL);

    //  Get rid of half-processed messages in the out pipe. Flush any
    //  unflushed messages upstream.
    _pipe->rollback ();
    _pipe->flush ();

    //  Remove any half-read message from the in pipe.
    while (_incomplete_in) {
        msg_t msg;
        int rc = msg.init ();
        errno_assert (rc == 0);
        rc = pull_msg (&msg);
        errno_assert (rc == 0);
        rc = msg.close ();
        errno_assert (rc == 0);
    }
}

void zmq::session_base_t::pipe_terminated (pipe_t *pipe_)
{
    // Drop the reference to the deallocated pipe if required.
    zmq_assert (pipe_ == _pipe || _terminating_pipes.count (pipe_) == 1);

    if (pipe_ == _pipe) {
        // If this is our current pipe, remove it
        _pipe = NULL;
        if (_has_linger_timer) {
            cancel_timer (linger_timer_id);
            _has_linger_timer = false;
        }
    } else
        // Remove the pipe from the detached pipes set
        _terminating_pipes.erase (pipe_);

    // raw_socket has been removed

    //  If we are waiting for pending messages to be sent, at this point
    //  we are sure that there will be no more messages and we can proceed
    //  with termination safely.
    if (_pending && !_pipe && _terminating_pipes.empty ()) {
        _pending = false;
        own_t::process_term (0);
    }
}

void zmq::session_base_t::read_activated (pipe_t *pipe_)
{
    // Skip activating if we're detaching this pipe
    if (unlikely (pipe_ != _pipe)) {
        zmq_assert (_terminating_pipes.count (pipe_) == 1);
        return;
    }

    if (unlikely (_engine == NULL)) {
        if (_pipe)
            _pipe->check_read ();
        return;
    }

    _engine->restart_output ();
}

void zmq::session_base_t::write_activated (pipe_t *pipe_)
{
    // Skip activating if we're detaching this pipe
    if (_pipe != pipe_) {
        zmq_assert (_terminating_pipes.count (pipe_) == 1);
        return;
    }

    if (_engine)
        _engine->restart_input ();
}

void zmq::session_base_t::hiccuped (pipe_t *)
{
    //  Hiccups are always sent from session to socket, not the other
    //  way round.
    zmq_assert (false);
}

zmq::socket_base_t *zmq::session_base_t::get_socket () const
{
    return _socket;
}

void zmq::session_base_t::process_plug ()
{
    if (_active)
        start_connecting (false);
}

void zmq::session_base_t::process_attach (i_engine *engine_)
{
    zmq_assert (engine_ != NULL);
    zmq_assert (!_engine);
    _engine = engine_;

    if (!engine_->has_handshake_stage ())
        engine_ready ();

    //  Plug in the engine.
    _engine->plug (_io_thread, this);
}

void zmq::session_base_t::engine_ready ()
{
    //  Create the pipe if it does not exist yet.
    if (!_pipe && !is_terminating ()) {
        object_t *parents[2] = {this, _socket};
        pipe_t *pipes[2] = {NULL, NULL};

        const bool conflate = get_effective_conflate_option (options);

        int hwms[2] = {conflate ? -1 : options.rcvhwm,
                       conflate ? -1 : options.sndhwm};
        bool conflates[2] = {conflate, conflate};
        const int rc = pipepair (parents, pipes, hwms, conflates);
        errno_assert (rc == 0);

        //  Plug the local end of the pipe.
        pipes[0]->set_event_sink (this);

        //  Remember the local end of the pipe.
        zmq_assert (!_pipe);
        _pipe = pipes[0];

        //  The endpoints strings are not set on bind, set them here so that
        //  events can use them.
        pipes[0]->set_endpoint_pair (_engine->get_endpoint ());
        pipes[1]->set_endpoint_pair (_engine->get_endpoint ());

        //  Ask socket to plug into the remote end of the pipe.
        send_bind (_socket, pipes[1]);
    }
}

void zmq::session_base_t::engine_error (bool handshaked_,
                                        zmq::i_engine::error_reason_t reason_)
{
    //  Engine is dead. Let's forget about it.
    _engine = NULL;

    //  Remove any half-done messages from the pipes.
    if (_pipe) {
        clean_pipes ();

        //  Only send disconnect message if socket was accepted and handshake was completed
        if (!_active && handshaked_ && options.can_recv_disconnect_msg
            && !options.disconnect_msg.empty ()) {
            _pipe->set_disconnect_msg (options.disconnect_msg);
            _pipe->send_disconnect_msg ();
        }

        //  Only send hiccup message if socket was connected and handshake was completed
        if (_active && handshaked_ && options.can_recv_hiccup_msg
            && !options.hiccup_msg.empty ()) {
            _pipe->send_hiccup_msg (options.hiccup_msg);
        }
    }

    zmq_assert (reason_ == i_engine::connection_error
                || reason_ == i_engine::timeout_error
                || reason_ == i_engine::protocol_error);

    switch (reason_) {
        case i_engine::timeout_error:
            /* FALLTHROUGH */
        case i_engine::connection_error:
            if (_active) {
                reconnect ();
                break;
            }

        case i_engine::protocol_error:
            if (_pending) {
                if (_pipe)
                    _pipe->terminate (false);
            } else {
                terminate ();
            }
            break;
    }

    //  Just in case there's only a delimiter in the pipe.
    if (_pipe)
        _pipe->check_read ();
}

void zmq::session_base_t::process_term (int linger_)
{
    zmq_assert (!_pending);

    //  If the termination of the pipe happens before the term command is
    //  delivered there's nothing much to do. We can proceed with the
    //  standard termination immediately.
    if (!_pipe && _terminating_pipes.empty ()) {
        own_t::process_term (0);
        return;
    }

    _pending = true;

    if (_pipe != NULL) {
        //  If there's finite linger value, delay the termination.
        //  If linger is infinite (negative) we don't even have to set
        //  the timer.
        if (linger_ > 0) {
            zmq_assert (!_has_linger_timer);
            add_timer (linger_, linger_timer_id);
            _has_linger_timer = true;
        }

        //  Start pipe termination process. Delay the termination till all messages
        //  are processed in case the linger time is non-zero.
        _pipe->terminate (linger_ != 0);

        //  TODO: Should this go into pipe_t::terminate ?
        //  In case there's no engine and there's only delimiter in the
        //  pipe it wouldn't be ever read. Thus we check for it explicitly.
        if (!_engine)
            _pipe->check_read ();
    }
}

void zmq::session_base_t::timer_event (int id_)
{
    //  Linger period expired. We can proceed with termination even though
    //  there are still pending messages to be sent.
    zmq_assert (id_ == linger_timer_id);
    _has_linger_timer = false;

    //  Ask pipe to terminate even though there may be pending messages in it.
    zmq_assert (_pipe);
    _pipe->terminate (false);
}

void zmq::session_base_t::process_conn_failed ()
{
    std::string *ep = new (std::string);
    _addr->to_string (*ep);
    send_term_endpoint (_socket, ep);
}

void zmq::session_base_t::reconnect ()
{
    //  For delayed connect situations, terminate the pipe
    //  and reestablish later on
    if (_pipe && options.immediate == 1) {
        _pipe->hiccup ();
        _pipe->terminate (false);
        _terminating_pipes.insert (_pipe);
        _pipe = NULL;

        if (_has_linger_timer) {
            cancel_timer (linger_timer_id);
            _has_linger_timer = false;
        }
    }

    reset ();

    //  Reconnect.
    if (options.reconnect_ivl > 0)
        start_connecting (true);
    else {
        std::string *ep = new (std::string);
        _addr->to_string (*ep);
        send_term_endpoint (_socket, ep);
    }

    //  For subscriber sockets we hiccup the inbound pipe, which will cause
    //  the socket object to resend all the subscriptions.
    if (_pipe
        && (options.type == ZMQ_SUB || options.type == ZMQ_XSUB))
        _pipe->hiccup ();
}

void zmq::session_base_t::start_connecting (bool wait_)
{
    zmq_assert (_active);

    //  Choose I/O thread to run connecter in. Given that we are already
    //  running in an I/O thread, there must be at least one available.
    io_thread_t *io_thread = choose_io_thread (options.affinity);
    zmq_assert (io_thread);

    //  Create the connecter object.
    own_t *connecter = NULL;
    if (_addr->protocol == protocol_name::tcp) {
        //  Use ASIO-based connecter for async_connect
        connecter = new (std::nothrow)
          asio_tcp_connecter_t (io_thread, this, options, _addr, wait_);
    }
#if defined ZMQ_HAVE_TLS && defined ZMQ_HAVE_ASIO_SSL
    else if (_addr->protocol == protocol_name::tls) {
        //  ASIO-only: Use ASIO-based TLS connecter with SSL/TLS encryption
        connecter = new (std::nothrow)
          asio_tls_connecter_t (io_thread, this, options, _addr, wait_);
    }
#endif
#if defined ZMQ_HAVE_IPC
    else if (_addr->protocol == protocol_name::ipc) {
        connecter = new (std::nothrow)
          asio_ipc_connecter_t (io_thread, this, options, _addr, wait_);
    }
#endif
#if defined ZMQ_HAVE_WS
    //  WebSocket transport (ws://, wss://)
    else if (_addr->protocol == protocol_name::ws
#if defined ZMQ_HAVE_WSS
             || _addr->protocol == protocol_name::wss
#endif
    ) {
        connecter = new (std::nothrow)
          asio_ws_connecter_t (io_thread, this, options, _addr, wait_);
    }
#endif
    if (connecter != NULL) {
        alloc_assert (connecter);
        launch_child (connecter);
        return;
    }

    zmq_assert (false);
}
