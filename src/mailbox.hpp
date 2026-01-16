/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_MAILBOX_HPP_INCLUDED__
#define __ZMQ_MAILBOX_HPP_INCLUDED__

#include <stddef.h>

#include "config.hpp"
#include "command.hpp"
#include "ypipe.hpp"
#include "mutex.hpp"
#include "i_mailbox.hpp"
#include "condition_variable.hpp"

#include <atomic>

namespace boost
{
namespace asio
{
class io_context;
}
}

namespace zmq
{
class mailbox_t ZMQ_FINAL : public i_mailbox
{
  public:
    mailbox_t ();
    ~mailbox_t ();

    void send (const command_t &cmd_);
    int recv (command_t *cmd_, int timeout_);

    bool valid () const;

    typedef void (*mailbox_handler_t) (void *arg_);
    typedef void (*mailbox_pre_post_t) (void *arg_);
    void set_io_context (boost::asio::io_context *io_context_,
                         mailbox_handler_t handler_,
                         void *handler_arg_,
                         mailbox_pre_post_t pre_post_ = NULL);
    void schedule_if_needed ();
    bool reschedule_if_needed ();

#ifdef HAVE_FORK
    // close the file descriptors in the signaller. This is used in a forked
    // child process to close the file descriptors so that they do not interfere
    // with the context in the parent process.
    void forked () ZMQ_FINAL
    {
        // TODO: call fork on the condition variable
    }
#endif

  private:
    //  The pipe to store actual commands.
    typedef ypipe_t<command_t, command_pipe_granularity> cpipe_t;
    cpipe_t _cpipe;

    //  Condition variable to signal waiting receivers.
    condition_variable_t _cond_var;

    //  There's only one thread receiving from the mailbox, but there
    //  is arbitrary number of threads sending. Given that ypipe requires
    //  synchronised access on both of its endpoints, we have to synchronise
    //  the sending side.
    mutex_t _sync;

    boost::asio::io_context *_io_context;
    mailbox_handler_t _handler;
    void *_handler_arg;
    mailbox_pre_post_t _pre_post;
    std::atomic<bool> _scheduled;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (mailbox_t)
};
}

#endif
