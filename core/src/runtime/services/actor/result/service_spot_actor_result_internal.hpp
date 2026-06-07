/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_ACTOR_RESULT_INTERNAL_HPP_INCLUDED
#define ZLINK_SERVICE_SPOT_ACTOR_RESULT_INTERNAL_HPP_INCLUDED

#include "zlink.h"

namespace zlink
{
namespace spot_actor_internal
{

zlink_request_result_t actor_missing_request_result_from_errno ();
zlink_submit_result_t actor_missing_submit_result_from_errno ();
zlink_request_result_t errno_to_request_result (int err_);
zlink_submit_result_t errno_to_submit_result (int err_);
zlink_submit_result_t request_result_to_submit_result (zlink_request_result_t result_);

}
}

#endif
