/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_spot_request_reply_internal.hpp"

int zlink::spot_reqrep_internal::dispatch_local_request (
  const std::string &router_rid_,
  std::vector<zlink_msg_t> *combined_)
{
    return dispatch_local_request_impl (router_rid_, combined_);
}
