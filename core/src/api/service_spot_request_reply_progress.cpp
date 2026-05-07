/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <map>
#include <memory>
#include <vector>

#include "api/service_api_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_subject_access.hpp"

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

extern "C" int zlink_spot_request_progress_internal (void *spot_)
{
    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return -1;
    }

    const std::shared_ptr<spot_request_reply_state_t> state =
      zlink::spot_reqrep_internal::try_find_spot_state (spot_);
    if (!state) {
        errno = 0;
        return 0;
    }

    int drained = 0;

    spot_progress_sources_t sources;
    snapshot_spot_progress_sources (state, &sources);
    if (sources.channel_reply_dealers.empty ()) {
        const int direct_rc =
          zlink::spot_reqrep_internal::drain_spot_reply_completions (
            state, spot_);
        if (direct_rc < 0)
            return -1;
        errno = 0;
        return direct_rc;
    }

    const int bridge_rc =
      drain_attached_channel_reply_bridge_progress_from_snapshot (
        sources.channel_reply_dealers);
    if (bridge_rc < 0)
        return -1;
    drained += bridge_rc;

    const int direct_rc =
      zlink::spot_reqrep_internal::drain_spot_reply_completions (state, spot_);
    if (direct_rc < 0)
        return -1;
    drained += direct_rc;

    if (sources.dispatch_handler_installed
        && !in_spot_dispatch_event_callback (spot_)) {
        errno = 0;
        return drained;
    }

    for (size_t i = 0; i < sources.channel_reply_dealers.size (); ++i) {
        const int rc =
          zlink::spot_reqrep_internal::drain_spot_channel_reply_completions_from (
            state, spot_, sources.channel_reply_dealers[i]);
        if (rc < 0 && errno != ENOENT)
            return -1;
        if (rc > 0)
            drained += rc;
    }

    errno = 0;
    return drained;
}

extern "C" int zlink_spot_channel_reply_progress_from (void *spot_,
                                                        void *dealer_)
{
    if (!as_spot_handle (spot_) || !as_socket_handle (dealer_).socket) {
        errno = EFAULT;
        return -1;
    }

    const std::shared_ptr<spot_request_reply_state_t> state =
      zlink::spot_reqrep_internal::try_find_spot_state (spot_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }

    {
        if (!zlink::spot_reqrep_internal::has_spot_channel_reply_source (state,
                                                                         dealer_)) {
            errno = ENOENT;
            return -1;
        }
    }

    const socket_handle_t dealer_handle = as_socket_handle (dealer_);
    const std::shared_ptr<reqrep::socket_request_reply_state_t> socket_state =
      reqrep::find_request_reply_state (dealer_handle);
    int drained = 0;
    if (socket_state) {
        const int rc = reqrep::drain_reply_completions (socket_state, dealer_);
        if (rc < 0)
            return -1;
        drained += rc;
    }

    const int source_rc =
      zlink::spot_reqrep_internal::drain_spot_channel_reply_completions_from (
        state, spot_, dealer_);
    if (source_rc < 0)
        return -1;
    drained += source_rc;

    errno = 0;
    return drained;
}

extern "C" int zlink_spot_request_channel_progress_internal (
  void *spot_,
  const char *channel_name_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node || !channel_name_ || channel_name_[0] == '\0') {
        errno = EFAULT;
        return -1;
    }

    LIBZLINK_UNUSED (channel_name_);
    return zlink_spot_request_progress_internal (spot_);
}
