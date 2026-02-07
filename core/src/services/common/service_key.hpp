/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_KEY_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_KEY_HPP_INCLUDED__

#include <string>

namespace zlink
{
namespace services
{
struct service_key_t
{
    unsigned short type;
    std::string name;
};
}
}

#endif
