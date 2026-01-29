/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_MAILBOX_SAFE_HPP_INCLUDED__
#define __ZLINK_MAILBOX_SAFE_HPP_INCLUDED__

#include <vector>
#include <stddef.h>

#include "core/signaler.hpp"
#include "utils/fd.hpp"
#include "utils/config.hpp"
#include "core/command.hpp"
#include "utils/mutex.hpp"
#include "utils/condition_variable.hpp"
#include "core/ypipe.hpp"
#include "core/i_mailbox.hpp"

#include <atomic>

namespace boost
{
namespace asio
{
class io_context;
}
}
namespace zlink
{
class mailbox_safe_t ZLINK_FINAL : public i_mailbox
{
  public:
    mailbox_safe_t (mutex_t *sync_);
    ~mailbox_safe_t ();

    void send (const command_t &cmd_);
    int recv (command_t *cmd_, int timeout_);

    // Add signaler to mailbox which will be called when a message is ready
    void add_signaler (signaler_t *signaler_);
    void remove_signaler (signaler_t *signaler_);
    void clear_signalers ();

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
    void forked () ZLINK_FINAL
    {
        // TODO: call fork on the condition variable
    }
#endif

  private:
    //  The pipe to store actual commands.
    typedef ypipe_t<command_t, command_pipe_granularity> cpipe_t;
    cpipe_t _cpipe;

    //  Condition variable to pass signals from writer thread to reader thread.
    condition_variable_t _cond_var;

    //  Synchronize access to the mailbox from receivers and senders
    mutex_t *const _sync;

    std::vector<zlink::signaler_t *> _signalers;

    boost::asio::io_context *_io_context;
    mailbox_handler_t _handler;
    void *_handler_arg;
    mailbox_pre_post_t _pre_post;
    std::atomic<bool> _scheduled;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (mailbox_safe_t)
};
}

#endif
