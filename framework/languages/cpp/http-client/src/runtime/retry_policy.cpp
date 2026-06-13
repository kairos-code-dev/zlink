/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/retry_policy.hpp"

namespace zlink::http_client::detail
{

retry_policy_t::retry_policy_t (const http_client_options_t &options,
                                const http_request_t &request) :
    _max_retries ((request.sink || request.body_provider) ? 0 : options.retry_attempts)
{
}

bool retry_policy_t::should_retry (
  int attempt,
  const zlink::framework::result_t<raw_http_response_t> &result) const
{
    return !result.has_value () && attempt < _max_retries && result.error ()->is_retriable ();
}

std::chrono::milliseconds retry_policy_t::delay ()
{
    return std::chrono::milliseconds (50);
}

} // namespace zlink::http_client::detail
