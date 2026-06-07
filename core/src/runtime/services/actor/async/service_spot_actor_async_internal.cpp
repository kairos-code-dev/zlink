/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/async/service_spot_actor_async_internal.hpp"

#include "api/socket/request_timeout_scheduler_internal.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

namespace
{
const uint32_t actor_async_timeout_precedence_delay_ms = 2;

struct actor_async_operation_t
{
    typedef void (*operation_complete_fn) (actor_async_operation_t *);
    typedef void (*operation_timeout_fn) (actor_async_operation_t *);

    actor_async_operation_t () :
        complete (NULL),
        timeout (NULL),
        reply_handler (NULL),
        lookup_handler (NULL),
        userdata (NULL),
        completed (false),
        refs (0),
        reply_run (NULL),
        lookup_run (NULL),
        arg (NULL),
        cleanup (NULL)
    {
    }

    operation_complete_fn complete;
    operation_timeout_fn timeout;
    std::mutex mutex;
    zlink_reply_handler_fn reply_handler;
    zlink_actor_lookup_handler_fn lookup_handler;
    void *userdata;
    bool completed;
    std::atomic<int> refs;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    zlink::spot_actor_async::reply_operation_run_fn reply_run;
    zlink::spot_actor_async::lookup_operation_run_fn lookup_run;
    void *arg;
    zlink::spot_actor_async::operation_cleanup_fn cleanup;
};

void complete_actor_reply_operation (actor_async_operation_t *operation_)
{
    const zlink_request_result_t result = operation_->reply_run
                                            ? operation_->reply_run (operation_->arg)
                                            : ZLINK_REQUEST_INTERNAL_ERROR;
    operation_->reply_handler (result, NULL, 0, operation_->userdata);
}

void timeout_actor_reply_operation (actor_async_operation_t *operation_)
{
    operation_->reply_handler (ZLINK_REQUEST_TIMED_OUT, NULL, 0, operation_->userdata);
}

void complete_actor_lookup_operation (actor_async_operation_t *operation_)
{
    zlink_actor_lookup_result_t result;
    memset (&result, 0, sizeof (result));
    if (operation_->lookup_run)
        operation_->lookup_run (operation_->arg, &result);
    else
        result.result = ZLINK_REQUEST_INTERNAL_ERROR;
    operation_->lookup_handler (&result, operation_->userdata);
}

void timeout_actor_lookup_operation (actor_async_operation_t *operation_)
{
    zlink_actor_lookup_result_t result;
    memset (&result, 0, sizeof (result));
    result.result = ZLINK_REQUEST_TIMED_OUT;
    operation_->lookup_handler (&result, operation_->userdata);
}

void retain_actor_async_operation (actor_async_operation_t *operation_)
{
    if (operation_)
        operation_->refs.fetch_add (1);
}

void release_actor_async_operation (actor_async_operation_t *operation_)
{
    if (!operation_)
        return;
    if (operation_->refs.fetch_sub (1) != 1)
        return;
    if (operation_->cleanup && operation_->arg)
        operation_->cleanup (operation_->arg);
    delete operation_;
}

bool claim_actor_async_operation (actor_async_operation_t *operation_)
{
    if (!operation_)
        return false;
    std::lock_guard<std::mutex> lock (operation_->mutex);
    if (operation_->completed)
        return false;
    operation_->completed = true;
    return true;
}

void complete_actor_async_operation_scheduled (void *userdata_)
{
    actor_async_operation_t *operation = static_cast<actor_async_operation_t *> (userdata_);
    if (!operation)
        return;
    if (claim_actor_async_operation (operation)) {
        zlink::request_timeout::cancel (operation->timeout_task);
        operation->timeout_task.reset ();
        if (operation->complete)
            operation->complete (operation);
    }
    release_actor_async_operation (operation);
}

void timeout_actor_async_operation_scheduled (void *userdata_)
{
    actor_async_operation_t *operation = static_cast<actor_async_operation_t *> (userdata_);
    if (!operation)
        return;
    if (claim_actor_async_operation (operation)) {
        if (operation->timeout)
            operation->timeout (operation);
    }
    release_actor_async_operation (operation);
}

void cleanup_actor_async_operation_timeout (void *userdata_)
{
    release_actor_async_operation (static_cast<actor_async_operation_t *> (userdata_));
}

zlink_submit_result_t schedule_actor_async_operation (actor_async_operation_t *operation_,
                                                      uint32_t timeout_ms_)
{
    retain_actor_async_operation (operation_);
    (void) zlink::request_timeout::schedule (actor_async_timeout_precedence_delay_ms,
                                             complete_actor_async_operation_scheduled, operation_);
    if (timeout_ms_ != 0) {
        retain_actor_async_operation (operation_);
        operation_->timeout_task =
          zlink::request_timeout::schedule (timeout_ms_, timeout_actor_async_operation_scheduled,
                                            operation_, cleanup_actor_async_operation_timeout);
    }
    return ZLINK_SUBMIT_OK;
}
}

namespace zlink
{
namespace spot_actor_async
{
zlink_submit_result_t schedule_reply_operation (zlink_reply_handler_fn handler_,
                                                void *userdata_,
                                                uint32_t timeout_ms_,
                                                reply_operation_run_fn run_,
                                                void *arg_,
                                                operation_cleanup_fn cleanup_)
{
    actor_async_operation_t *operation = new (std::nothrow) actor_async_operation_t;
    if (!operation) {
        if (cleanup_ && arg_)
            cleanup_ (arg_);
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    operation->complete = complete_actor_reply_operation;
    operation->timeout = timeout_actor_reply_operation;
    operation->reply_handler = handler_;
    operation->userdata = userdata_;
    operation->reply_run = run_;
    operation->arg = arg_;
    operation->cleanup = cleanup_;
    return schedule_actor_async_operation (operation, timeout_ms_);
}

zlink_submit_result_t schedule_lookup_operation (zlink_actor_lookup_handler_fn handler_,
                                                 void *userdata_,
                                                 uint32_t timeout_ms_,
                                                 lookup_operation_run_fn run_,
                                                 void *arg_,
                                                 operation_cleanup_fn cleanup_)
{
    actor_async_operation_t *operation = new (std::nothrow) actor_async_operation_t;
    if (!operation) {
        if (cleanup_ && arg_)
            cleanup_ (arg_);
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    operation->complete = complete_actor_lookup_operation;
    operation->timeout = timeout_actor_lookup_operation;
    operation->lookup_handler = handler_;
    operation->userdata = userdata_;
    operation->lookup_run = run_;
    operation->arg = arg_;
    operation->cleanup = cleanup_;
    return schedule_actor_async_operation (operation, timeout_ms_);
}
}
}
