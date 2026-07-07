/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/actor/spot/service_spot_actor_state_internal.hpp"
#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"

namespace
{

void cleanup_no_bind_timeout_key (void *userdata_)
{
    delete static_cast<zlink::spot_actor_api_internal::no_bind_pending_key_t *> (userdata_);
}

void on_no_bind_request_timeout (void *userdata_)
{
    const zlink::spot_actor_api_internal::no_bind_pending_key_t *key =
      static_cast<const zlink::spot_actor_api_internal::no_bind_pending_key_t *> (userdata_);
    if (!key)
        return;

    zlink::spot_actor_api_internal::no_bind_pending_reply_t pending;
    if (zlink::spot_actor_api_internal::actor_runtime ().no_bind.take_pending (*key, &pending)) {
        zlink::request_reply::complete_reply_callback (pending.handler, ETIMEDOUT, NULL, 0,
                                                       pending.userdata);
    }
}

}

namespace zlink
{
namespace spot_actor_api_internal
{

int actor_no_bind_pending_state_t::register_pending (const no_bind_pending_key_t &key_,
                                                     zlink_reply_handler_fn handler_,
                                                     void *userdata_,
                                                     uint32_t timeout_ms_)
{
    if (!handler_ || key_.request_id == 0) {
        errno = EINVAL;
        return -1;
    }

    no_bind_pending_reply_t pending;
    pending.handler = handler_;
    pending.userdata = userdata_;
    if (timeout_ms_ != 0) {
        no_bind_pending_key_t *timeout_key = new (std::nothrow) no_bind_pending_key_t (key_);
        if (!timeout_key) {
            errno = ENOMEM;
            return -1;
        }
        pending.timeout_task = zlink::request_timeout::schedule (
          timeout_ms_, on_no_bind_request_timeout, timeout_key, cleanup_no_bind_timeout_key);
    }

    {
        std::lock_guard<std::mutex> lock (mutex);
        replies[key_] = pending;
    }
    return 0;
}

void actor_no_bind_pending_state_t::erase_pending (const no_bind_pending_key_t &key_)
{
    no_bind_pending_reply_t pending;
    {
        std::lock_guard<std::mutex> lock (mutex);
        std::map<no_bind_pending_key_t, no_bind_pending_reply_t>::iterator it = replies.find (key_);
        if (it == replies.end ())
            return;
        pending = it->second;
        replies.erase (it);
    }
    zlink::request_timeout::cancel (pending.timeout_task);
}

bool actor_no_bind_pending_state_t::take_pending (const no_bind_pending_key_t &key_,
                                                  no_bind_pending_reply_t *out_)
{
    if (!out_)
        return false;
    std::lock_guard<std::mutex> lock (mutex);
    std::map<no_bind_pending_key_t, no_bind_pending_reply_t>::iterator it = replies.find (key_);
    if (it == replies.end ())
        return false;
    *out_ = it->second;
    replies.erase (it);
    return true;
}

}
}
