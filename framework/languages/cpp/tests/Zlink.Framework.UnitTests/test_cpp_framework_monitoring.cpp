/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/diagnostics/runtime_metrics.hpp"

#include <cstddef>
#include <iostream>
#include <memory>

namespace
{

bool unsubscribed_metrics_remain_disabled ()
{
    auto state = std::make_shared<zlink::framework::detail::monitoring_runtime_state_t> ();
    const zlink::framework::runtime::runtime_metrics_t metrics (state);

    for (std::size_t index = 0; index < 10'000; ++index) {
        metrics.counter ("zlink.test.unsubscribed", "{operation}", 1,
                         {{"operation", "request"}});
    }

    // A disabled logger is the entire metric admission boundary. Emission must
    // remain a no-op without creating a second metric storage path.
    return !metrics.enabled ();
}

} // namespace

int main ()
{
    if (!unsubscribed_metrics_remain_disabled ()) {
        std::cerr << "unsubscribed metric emission unexpectedly became enabled\n";
        return 1;
    }
    return 0;
}
