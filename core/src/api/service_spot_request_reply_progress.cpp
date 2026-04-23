/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <map>
#include <memory>
#include <vector>

#include "api/service_api_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_request_reply_internal.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;

using zlink::spot_reqrep_internal::spot_request_reply_state_t;
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

    bool dispatch_handler_installed = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        dispatch_handler_installed = state->dispatch.handler != NULL;
    }

    int drained = 0;
    const bool has_direct_reply_completions =
      zlink::spot_reqrep_internal::has_spot_reply_completions (state);
    std::vector<void *> dealers;
    zlink::spot_reqrep_internal::snapshot_spot_channel_reply_dealers (state,
                                                                      &dealers);
    const bool has_channel_reply_sources = !dealers.empty ();
    if (!has_direct_reply_completions && !has_channel_reply_sources) {
        errno = 0;
        return 0;
    }
    if (has_channel_reply_sources) {
        const int bridge_rc =
          zlink::spot_reqrep_internal::
            drain_attached_channel_reply_bridge_progress (state);
        if (bridge_rc < 0)
            return -1;
        drained += bridge_rc;
    }

    if (has_direct_reply_completions) {
        const int direct_rc =
          zlink::spot_reqrep_internal::drain_spot_reply_completions (
            state, spot_);
        if (direct_rc < 0)
            return -1;
        drained += direct_rc;
    }

    if (dispatch_handler_installed && !in_spot_dispatch_event_callback (spot_)) {
        errno = 0;
        return drained;
    }
    if (!has_channel_reply_sources) {
        errno = 0;
        return drained;
    }

    for (size_t i = 0; i < dealers.size (); ++i) {
        const int rc =
          zlink::spot_reqrep_internal::drain_spot_channel_reply_completions_from (
            state, spot_, dealers[i]);
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
