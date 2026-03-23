/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <boost/asio.hpp>

#include "core/io_thread.hpp"
#include "core/mailbox.hpp"
#include "sockets/socket_base.hpp"

void zlink::socket_base_t::reaper_mailbox_handler (void *arg_)
{
    socket_base_t *self = static_cast<socket_base_t *> (arg_);
    self->in_event ();
    self->dec_mailbox_ref ();
}

void zlink::socket_base_t::reaper_mailbox_pre_post (void *arg_)
{
    socket_base_t *self = static_cast<socket_base_t *> (arg_);
    self->inc_mailbox_ref ();
}

void zlink::socket_base_t::async_mailbox_handler (void *arg_)
{
    socket_base_t *self = static_cast<socket_base_t *> (arg_);
    self->process_async_mailbox ();
    self->dec_mailbox_ref ();
}

void zlink::socket_base_t::async_mailbox_pre_post (void *arg_)
{
    socket_base_t *self = static_cast<socket_base_t *> (arg_);
    self->inc_mailbox_ref ();
}

void zlink::socket_base_t::start_reaping (poller_t *poller_)
{
    //  Safety net: if the async mailbox handler is still running
    //  (e.g. close() was bypassed or quiesce timed out), wait here
    //  before the reaper touches socket internal state.
    if (_async_quiesce_pending.load (std::memory_order_acquire))
        wait_async_quiesced (1000);

    //  Plug the socket to the reaper thread.
    _poller = poller_;

    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->set_io_context (&_poller->get_io_context (),
                             &socket_base_t::reaper_mailbox_handler, this,
                             &socket_base_t::reaper_mailbox_pre_post);
    mailbox->schedule_if_needed ();

    //  Initialise the termination and check whether it can be deallocated
    //  immediately.
    terminate ();
    check_destroy ();
}

int zlink::socket_base_t::process_commands (int timeout_, bool throttle_)
{
    if (timeout_ == 0) {
        //  If we are asked not to wait, check whether we haven't processed
        //  commands recently, so that we can throttle the new commands.

        //  Get the CPU's tick counter. If 0, the counter is not available.
        const uint64_t tsc = zlink::clock_t::rdtsc ();

        //  Optimised version of command processing - it doesn't have to check
        //  for incoming commands each time. It does so only if certain time
        //  elapsed since last command processing. Command delay varies
        //  depending on CPU speed: It's ~1ms on 3GHz CPU, ~2ms on 1.5GHz CPU
        //  etc. The optimisation makes sense only on platforms where getting
        //  a timestamp is a very cheap operation (tens of nanoseconds).
        if (tsc && throttle_) {
            //  Check whether TSC haven't jumped backwards (in case of migration
            //  between CPU cores) and whether certain time have elapsed since
            //  last command processing. If it didn't do nothing.
            if (tsc >= _last_tsc && tsc - _last_tsc <= max_command_delay)
                return 0;
            _last_tsc = tsc;
        }
    }

    //  Check whether there are any commands pending for this thread.
    command_t cmd;
    int rc = _mailbox->recv (&cmd, timeout_);

    if (rc != 0 && errno == EINTR)
        return -1;

    //  Process all available commands.
    while (rc == 0 || errno == EINTR) {
        if (rc == 0)
            cmd.destination->process_command (cmd);
        rc = _mailbox->recv (&cmd, 0);
    }

    zlink_assert (errno == EAGAIN);

    if (_ctx_terminated) {
        errno = ETERM;
        return -1;
    }

    return 0;
}

int zlink::socket_base_t::start_async_mailbox_processing (
  io_thread_t *io_thread_)
{
    if (!io_thread_) {
        errno = EINVAL;
        return -1;
    }

    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    _async_mailbox_active.store (true, std::memory_order_release);
    mailbox->set_io_context (&io_thread_->get_io_context (),
                             &socket_base_t::async_mailbox_handler, this,
                             &socket_base_t::async_mailbox_pre_post);
    mailbox->schedule_if_needed ();
    return 0;
}

void zlink::socket_base_t::stop_async_mailbox_processing ()
{
    _async_mailbox_active.store (false, std::memory_order_release);
    _async_processing_done.store (false, std::memory_order_release);
    _async_quiesce_pending.store (true, std::memory_order_release);
    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->schedule_if_needed ();
}

void zlink::socket_base_t::wait_async_quiesced (int timeout_ms_)
{
    if (_async_processing_done.load (std::memory_order_acquire))
        return;
    scoped_lock_t lock (_async_done_mu);
    while (!_async_processing_done.load (std::memory_order_acquire)) {
        const int rc =
          _async_done_cv.wait (&_async_done_mu,
                               timeout_ms_ > 0 ? timeout_ms_ : 2000);
        if (rc != 0)
            break;
    }
}

void zlink::socket_base_t::process_stop ()
{
    //  Here, someone have called zlink_ctx_term while the socket was still alive.
    //  We'll remember the fact so that any blocking call is interrupted and any
    //  further attempt to use the socket will return ETERM. The user is still
    //  responsible for calling zlink_close on the socket though!
    scoped_lock_t lock (_monitor_sync);
    stop_monitor ();

    _ctx_terminated = true;
}

void zlink::socket_base_t::process_bind (pipe_t *pipe_)
{
    attach_pipe (pipe_);
}

void zlink::socket_base_t::process_term (int linger_)
{
    //  Unregister all inproc endpoints associated with this socket.
    //  Doing this we make sure that no new pipes from other sockets (inproc)
    //  will be initiated.
    unregister_endpoints (this);

    //  Ask all attached pipes to terminate.
    for (pipes_t::size_type i = 0, size = _pipes.size (); i != size; ++i) {
        //  Only inprocs might have a disconnect message set
        _pipes[i]->send_disconnect_msg ();
        _pipes[i]->terminate (false);
    }
    register_term_acks (static_cast<int> (_pipes.size ()));
    _term_pipe_acks_registered = static_cast<int> (_pipes.size ());
    _term_pipe_acks_received = 0;

    //  Continue the termination process immediately.
    own_t::process_term (linger_);
}

void zlink::socket_base_t::process_term_endpoint (std::string *endpoint_)
{
    term_endpoint (endpoint_->c_str ());
    delete endpoint_;
}

void zlink::socket_base_t::set_all_pipes_nodelay ()
{
    for (pipes_t::size_type i = 0, size = _pipes.size (); i != size; ++i) {
        if (_pipes[i])
            _pipes[i]->set_nodelay ();
    }
}

void zlink::socket_base_t::update_pipe_options (int option_)
{
    if (option_ == ZLINK_INTERNAL_OPT_SNDHWM
        || option_ == ZLINK_INTERNAL_OPT_RCVHWM) {
        for (pipes_t::size_type i = 0, size = _pipes.size (); i != size; ++i) {
            _pipes[i]->set_hwms (options.rcvhwm, options.sndhwm);
            _pipes[i]->send_hwms_to_peer (options.sndhwm, options.rcvhwm);
        }
    }
}

void zlink::socket_base_t::process_destroy ()
{
    _destroyed = true;
}

void zlink::socket_base_t::in_event ()
{
    do {
        //  This function is invoked only once the socket is running in the
        //  context of the reaper thread. Process any commands from other
        //  threads/sockets that may be available at the moment. Ultimately,
        //  the socket will be destroyed.
        process_commands (0, false);
        if (_destroyed) {
            check_destroy ();
            return;
        }
    } while (static_cast<mailbox_t *> (_mailbox)->reschedule_if_needed ());
}

void zlink::socket_base_t::process_async_mailbox ()
{
    do {
        process_commands (0, false);
        if (_destroyed) {
            check_destroy ();
            return;
        }
        if (_async_mailbox_active.load (std::memory_order_acquire))
            xdispatch_io ();
        if (!_async_mailbox_active.load (std::memory_order_acquire)) {
            mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
            mailbox->reschedule_if_needed ();
            mailbox->set_io_context (NULL, NULL, NULL, NULL);
            //  Signal quiesce completion to waiting close()/start_reaping().
            if (_async_quiesce_pending.load (std::memory_order_acquire)) {
                _async_quiesce_pending.store (false,
                                              std::memory_order_release);
                _async_processing_done.store (true,
                                              std::memory_order_release);
                scoped_lock_t lock (_async_done_mu);
                _async_done_cv.broadcast ();
            }
            return;
        }
    } while (static_cast<mailbox_t *> (_mailbox)->reschedule_if_needed ());
}

void zlink::socket_base_t::out_event ()
{
    zlink_assert (false);
}

void zlink::socket_base_t::timer_event (int)
{
    zlink_assert (false);
}

void zlink::socket_base_t::check_destroy ()
{
    //  If the object was already marked as destroyed, finish the deallocation.
    if (_destroyed) {
        _destroy_pending = true;
        if (_mailbox_refcnt.add (0) != 0)
            return;

        inc_mailbox_ref ();
        if (_poller) {
            boost::asio::post (_poller->get_io_context (), [this]() {
                this->dec_mailbox_ref ();
            });
        } else {
            dec_mailbox_ref ();
        }
    }
}

void zlink::socket_base_t::inc_mailbox_ref ()
{
    _mailbox_refcnt.add (1);
}

void zlink::socket_base_t::dec_mailbox_ref ()
{
    if (_mailbox_refcnt.sub (1) || !_destroy_pending)
        return;

    finalize_destroy ();
}

void zlink::socket_base_t::finalize_destroy ()
{
    _destroy_pending = false;

    //  Remove the socket from the context.
    destroy_socket (this);

    //  Notify the reaper about the fact.
    send_reaped ();

    //  Deallocate.
    own_t::process_destroy ();
}
