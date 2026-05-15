/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <map>
#include <memory>
#include <vector>

#include "api/service/service_api_internal.hpp"
#include "api/spot/dispatch/service_spot_dispatch_context_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/pubsub/spot_subject_access.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;

using zlink::spot_reqrep_internal::spot_request_reply_state_t;

struct spot_progress_sources_t
{
    spot_progress_sources_t () : dispatch_handler_installed (false) {}

    bool dispatch_handler_installed;
    std::vector<void *> channel_reply_dealers;
};

void snapshot_spot_progress_sources (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  spot_progress_sources_t *sources_)
{
    sources_->channel_reply_dealers.clear ();

    std::lock_guard<std::mutex> lock (state_->mutex);
    sources_->dispatch_handler_installed = state_->dispatch.handler != NULL;
    sources_->channel_reply_dealers.reserve (
      state_->completion_state.channel_reply_sources.size ());
    for (std::map<void *,
                  std::shared_ptr<
                    zlink::spot_reqrep_internal::spot_channel_reply_source_t> >::
           const_iterator it =
             state_->completion_state.channel_reply_sources.begin ();
         it != state_->completion_state.channel_reply_sources.end (); ++it) {
        sources_->channel_reply_dealers.push_back (it->first);
    }
}

int drain_attached_channel_reply_bridge_progress_from_snapshot (
  const std::vector<void *> &dealers_)
{
    int drained = 0;
    for (size_t i = 0; i < dealers_.size (); ++i) {
        socket_handle_t handle = as_socket_handle (dealers_[i]);
        if (!handle.socket)
            continue;
        const std::shared_ptr<reqrep::socket_request_reply_state_t>
          socket_state = reqrep::find_request_reply_state (handle);
        if (!socket_state)
            continue;
        const int rc = reqrep::drain_reply_completions (socket_state,
                                                        dealers_[i]);
        if (rc < 0)
            return -1;
        drained += rc;
    }
    errno = 0;
    return drained;
}
}
