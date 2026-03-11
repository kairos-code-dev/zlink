/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_ADVERTISE_ENDPOINT_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_ADVERTISE_ENDPOINT_HPP_INCLUDED__

#include <cstring>
#include <string>

namespace zlink
{
namespace services
{
inline std::string normalize_advertise_endpoint (
  const char *advertise_endpoint_,
  const std::string &bound_endpoint_)
{
    if (advertise_endpoint_ && advertise_endpoint_[0] != '\0')
        return advertise_endpoint_;

    if (bound_endpoint_.empty ())
        return std::string ();

    std::string endpoint = bound_endpoint_;
    if (endpoint.find ("tcp://") == 0) {
        const std::string::size_type host_start = strlen ("tcp://");
        const std::string::size_type colon = endpoint.find (':', host_start);
        if (colon != std::string::npos) {
            const std::string host =
              endpoint.substr (host_start, colon - host_start);
            if (host == "*" || host == "0.0.0.0")
                endpoint.replace (host_start, host.size (), "127.0.0.1");
        }
    }

    return endpoint;
}
}
}

#endif
