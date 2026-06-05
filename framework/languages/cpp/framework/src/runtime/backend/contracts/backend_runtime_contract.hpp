/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::framework::detail
{

struct backend_runtime_contract_t
{
    std::string backend_name;
    bool owns_native_context = true;
    bool projects_dispatch_events = true;
};

} // namespace zlink::framework::detail
