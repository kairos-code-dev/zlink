/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/result/service_spot_actor_result_internal.hpp"

#include "api/message/request_result_internal.hpp"
#include "api/message/submit_result_internal.hpp"

#include <errno.h>

namespace zlink
{
namespace spot_actor_internal
{

zlink_request_result_t actor_missing_request_result_from_errno ()
{
    return errno == ESTALE ? ZLINK_REQUEST_CONFLICT : ZLINK_REQUEST_NOT_FOUND;
}

zlink_submit_result_t actor_missing_submit_result_from_errno ()
{
    return errno == ESTALE ? ZLINK_SUBMIT_INVALID_STATE
                           : ZLINK_SUBMIT_NOT_FOUND;
}

zlink_request_result_t errno_to_request_result (int err_)
{
    return request_result_internal::from_errno (err_);
}

zlink_submit_result_t errno_to_submit_result (int err_)
{
    return submit_result_internal::from_errno (err_);
}

zlink_submit_result_t request_result_to_submit_result (
  zlink_request_result_t result_)
{
    switch (result_) {
    case ZLINK_REQUEST_OK:
        return ZLINK_SUBMIT_OK;
    case ZLINK_REQUEST_NOT_CONNECTED:
        return ZLINK_SUBMIT_NOT_CONNECTED;
    case ZLINK_REQUEST_NOT_FOUND:
        return ZLINK_SUBMIT_NOT_FOUND;
    case ZLINK_REQUEST_CONFLICT:
    case ZLINK_REQUEST_INVALID_STATE:
    case ZLINK_REQUEST_BUSY:
        return ZLINK_SUBMIT_INVALID_STATE;
    case ZLINK_REQUEST_NOT_SUPPORTED:
        return ZLINK_SUBMIT_NOT_SUPPORTED;
    default:
        return ZLINK_SUBMIT_INTERNAL_ERROR;
    }
}

}
}
