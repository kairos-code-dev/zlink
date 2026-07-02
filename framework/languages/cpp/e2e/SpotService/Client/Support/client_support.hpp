/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

} // namespace zlink::framework::e2e::spot_service::client
