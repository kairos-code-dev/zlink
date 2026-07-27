/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include <zlink/framework.hpp>

#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::trigger
{

template <typename TOperation> std::string capture_validation_error (TOperation operation)
{
    try {
        operation ();
    } catch (const std::exception &ex) {
        return ex.what ();
    }
    throw std::runtime_error ("expected monitoring validation failure");
}

inline std::string verify_duplicate_socket_source ()
{
    const auto error = capture_validation_error ([] {
        auto app = zlink::framework::app_t::create ();
        app.monitoring ().add_socket_events ("duplicate.source");
        app.monitoring ().add_socket_events ("duplicate.source");
    });
    return "mon-b2|duplicate=" + error;
}

inline std::string verify_polling_interval ()
{
    using namespace std::chrono_literals;
    const auto error = capture_validation_error ([] {
        auto app = zlink::framework::app_t::create ();
        app.monitoring ().add_location_events ("location-runtime", 0ms);
    });
    return "mon-b2|interval=" + error;
}

inline std::string verify_missing_socket_source ()
{
    const auto error = capture_validation_error ([] {
        auto app = zlink::framework::app_t::create ();
        app.monitoring ().add_socket_events ("missing.server");
        app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &) {});
    });
    return "mon-b2|missing-socket=" + error;
}

} // namespace zlink::framework::e2e::runtime_monitoring::trigger
