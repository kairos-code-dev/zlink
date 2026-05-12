/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_spot_actor_async_internal.hpp"

#include "api/request_timeout_scheduler_internal.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

namespace
{
const uint32_t actor_async_timeout_precedence_delay_ms = 2;

enum actor_async_operation_kind_t
{
    actor_async_reply,
    actor_async_lookup
};

struct actor_async_operation_t
{
    actor_async_operation_t () :
        kind (actor_async_reply),
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

    actor_async_operation_kind_t kind;
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
    actor_async_operation_t *operation =
      static_cast<actor_async_operation_t *> (userdata_);
    if (!operation)
        return;
    if (claim_actor_async_operation (operation)) {
        zlink::request_timeout::cancel (operation->timeout_task);
        operation->timeout_task.reset ();
        if (operation->kind == actor_async_reply) {
            const zlink_request_result_t result =
              operation->reply_run ? operation->reply_run (operation->arg)
                                   : ZLINK_REQUEST_INTERNAL_ERROR;
            operation->reply_handler (result, NULL, 0, operation->userdata);
        } else {
            zlink_actor_lookup_result_t result;
            memset (&result, 0, sizeof (result));
            if (operation->lookup_run)
                operation->lookup_run (operation->arg, &result);
            else
                result.result = ZLINK_REQUEST_INTERNAL_ERROR;
            operation->lookup_handler (&result, operation->userdata);
        }
    }
    release_actor_async_operation (operation);
}

void timeout_actor_async_operation_scheduled (void *userdata_)
{
    actor_async_operation_t *operation =
      static_cast<actor_async_operation_t *> (userdata_);
    if (!operation)
        return;
    if (claim_actor_async_operation (operation)) {
        if (operation->kind == actor_async_reply) {
            operation->reply_handler (ZLINK_REQUEST_TIMED_OUT, NULL, 0,
                                      operation->userdata);
        } else {
            zlink_actor_lookup_result_t result;
            memset (&result, 0, sizeof (result));
            result.result = ZLINK_REQUEST_TIMED_OUT;
            operation->lookup_handler (&result, operation->userdata);
        }
    }
    release_actor_async_operation (operation);
}

void cleanup_actor_async_operation_timeout (void *userdata_)
{
    release_actor_async_operation (
      static_cast<actor_async_operation_t *> (userdata_));
}

zlink_submit_result_t schedule_actor_async_operation (
  actor_async_operation_t *operation_, uint32_t timeout_ms_)
{
    retain_actor_async_operation (operation_);
    (void) zlink::request_timeout::schedule (
      actor_async_timeout_precedence_delay_ms,
      complete_actor_async_operation_scheduled, operation_);
    if (timeout_ms_ != 0) {
        retain_actor_async_operation (operation_);
        operation_->timeout_task = zlink::request_timeout::schedule (
          timeout_ms_, timeout_actor_async_operation_scheduled, operation_,
          cleanup_actor_async_operation_timeout);
    }
    return ZLINK_SUBMIT_OK;
}
}

namespace zlink
{
namespace spot_actor_async
{
zlink_submit_result_t schedule_reply_operation (
  zlink_reply_handler_fn handler_, void *userdata_, uint32_t timeout_ms_,
  reply_operation_run_fn run_, void *arg_, operation_cleanup_fn cleanup_)
{
    actor_async_operation_t *operation =
      new (std::nothrow) actor_async_operation_t;
    if (!operation) {
        if (cleanup_ && arg_)
            cleanup_ (arg_);
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    operation->kind = actor_async_reply;
    operation->reply_handler = handler_;
    operation->userdata = userdata_;
    operation->reply_run = run_;
    operation->arg = arg_;
    operation->cleanup = cleanup_;
    return schedule_actor_async_operation (operation, timeout_ms_);
}

zlink_submit_result_t schedule_lookup_operation (
  zlink_actor_lookup_handler_fn handler_, void *userdata_, uint32_t timeout_ms_,
  lookup_operation_run_fn run_, void *arg_, operation_cleanup_fn cleanup_)
{
    actor_async_operation_t *operation =
      new (std::nothrow) actor_async_operation_t;
    if (!operation) {
        if (cleanup_ && arg_)
            cleanup_ (arg_);
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    operation->kind = actor_async_lookup;
    operation->lookup_handler = handler_;
    operation->userdata = userdata_;
    operation->lookup_run = run_;
    operation->arg = arg_;
    operation->cleanup = cleanup_;
    return schedule_actor_async_operation (operation, timeout_ms_);
}
}
}
