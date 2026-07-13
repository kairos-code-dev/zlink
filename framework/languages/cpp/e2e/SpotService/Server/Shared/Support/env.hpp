/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <cstdlib>
#include <string>

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

} // namespace
