/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/runtime_errors.hpp"

namespace zlink::http_client::detail
{

zlink::framework::framework_exception_t request_error (const std::string &message)
{
    return zlink::framework::framework_exception_t (
      zlink::framework::framework_error_kind_t::request_failed, message);
}

zlink::framework::result_t<raw_http_response_t> timeout_before_exchange ()
{
    return zlink::framework::result_t<raw_http_response_t>::failure (
      zlink::framework::framework_error_kind_t::timeout,
      "HTTP request timed out before the scheduler started it", true);
}

zlink::framework::result_t<raw_http_response_t> map_exception (const std::exception &ex)
{
    return zlink::framework::result_t<raw_http_response_t>::failure (
      zlink::framework::framework_error_kind_t::request_failed, ex.what (), true);
}

} // namespace zlink::http_client::detail
