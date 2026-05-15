/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink.hpp>

#include <type_traits>

static_assert (std::is_class<zlink::context_t>::value,
               "public projection must expose context_t");
static_assert (std::is_class<zlink::message_t>::value,
               "public projection must expose message_t");
static_assert (std::is_class<zlink::received_t>::value,
               "public projection must expose received_t");
static_assert (std::is_class<zlink::dealer_socket_t>::value,
               "public projection must expose dealer_socket_t");
static_assert (std::is_class<zlink::router_socket_t>::value,
               "public projection must expose router_socket_t");
static_assert (std::is_class<zlink::monitor_handle_t>::value,
               "public projection must expose monitor_handle_t");
static_assert (std::is_class<zlink::service::spot_t>::value,
               "public projection must expose service::spot_t");
static_assert (std::is_class<zlink::socket_handle_t>::value,
               "public projection must expose socket_handle_t");
static_assert (std::is_class<zlink::timer_t>::value,
               "public projection must expose timer_t");
static_assert (std::is_class<zlink::atomic_counter_t>::value,
               "public projection must expose atomic_counter_t");
static_assert (std::is_class<zlink::stopwatch_t>::value,
               "public projection must expose stopwatch_t");
static_assert (std::is_class<zlink::thread_t>::value,
               "public projection must expose thread_t");

int main () { return 0; }
