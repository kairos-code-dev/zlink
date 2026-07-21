/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/diagnostics/runtime_metrics.hpp"

#include <cstddef>
#include <iostream>
#include <memory>

namespace
{

bool unsubscribed_metric_storage_unchanged ()
{
    auto state = std::make_shared<zlink::framework::detail::monitoring_runtime_state_t> ();
    const zlink::framework::runtime::runtime_metrics_t metrics (state);
    const auto handler_types_before = state->handlers.size ();

    for (std::size_t index = 0; index < 10'000; ++index) {
        metrics.counter ("zlink.test.unsubscribed", "{operation}", 1,
                         {{"operation", "request"}});
    }

    return !metrics.enabled () && state->handlers.size () == handler_types_before;
}

} // namespace

int main ()
{
    if (!unsubscribed_metric_storage_unchanged ()) {
        std::cerr << "unsubscribed metric emission changed internal storage\n";
        return 1;
    }
    return 0;
}
