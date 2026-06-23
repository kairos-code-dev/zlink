/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/discovery_registry_connection.hpp"

#include <zlink.hpp>

#include <chrono>
#include <exception>
#include <thread>

namespace zlink::framework::detail
{

void connect_registry_with_retry (zlink::service::discovery_t &discovery,
                                  const std::string &endpoint)
{
    std::exception_ptr last_error;
    for (int attempt = 0; attempt < 100; ++attempt) {
        try {
            discovery.connect_registry (endpoint);
            return;
        }
        catch (...) {
            last_error = std::current_exception ();
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    }
    if (last_error) {
        std::rethrow_exception (last_error);
    }
}

} // namespace zlink::framework::detail
