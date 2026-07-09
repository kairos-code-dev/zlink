/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_NATIVE_ARRAY_FETCH_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_NATIVE_ARRAY_FETCH_HPP_INCLUDED

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink.h>

#include <cerrno>
#include <vector>

namespace zlink::service::detail
{

template <typename NativeT, typename CountFn, typename FetchFn>
inline std::vector<NativeT> fetch_growable_native_array (CountFn count_, FetchFn fetch_)
{
    size_t count = 0;
    zlink::detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (count_ (&count)));

    std::vector<NativeT> native (count);
    if (count == 0)
        return native;

    while (true) {
        const auto result = static_cast<config_result_t> (fetch_ (native.data (), &count));
        if (result == config_result_t::ok) {
            native.resize (count);
            return native;
        }
        if (result == config_result_t::internal_error && zlink_errno () == ENOBUFS) {
            native.resize (count);
            continue;
        }
        zlink::detail::throw_if_failed<config_error_t> (result);
    }
}

} // namespace zlink::service::detail

#endif
