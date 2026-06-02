/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>
#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/error.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/eventing/events.hpp>
#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>
#include <zlink/framework/contracts/registry/registry.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_connector/contracts/version.hpp>

#include <future>
#include <type_traits>

static_assert (zlink::framework::version_major == 0);
static_assert (zlink::stream_connector::version_major == 0);
static_assert (
  !std::is_same_v<zlink::framework::task_t<int>, std::future<int>>);
static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::request_call_t<int>> ().submit ()),
    zlink::framework::task_t<int>>);

template<typename T>
concept has_blocking_wait = requires (T value) {
  value.wait ();
};

template<typename T>
concept has_future_get = requires (T value) {
  value.get ();
};

static_assert (!has_blocking_wait<zlink::framework::task_t<int>>);
static_assert (!has_future_get<zlink::framework::task_t<int>>);

namespace
{

struct named_request_t
{
  static constexpr const char *packet_name = "NamedRequest";
};

struct named_reply_t
{
};

class named_handler_t
{
public:
  named_reply_t handle (const named_request_t &) { return {}; }
};

} // namespace

int
main ()
{
  auto callback_kind = zlink::framework::framework_error_kind_t::request_failed;
  zlink::framework::request_call_t<int> call (
    zlink::framework::result_t<int>::failure (
      zlink::framework::framework_error_kind_t::timeout,
      "timeout"));

  call.submit ([&](zlink::framework::result_t<int> result) {
    callback_kind = result.error_kind ();
  });

  auto task = call.submit ();
  const auto coroutine_kind = task.result ().error_kind ();
  if (callback_kind != coroutine_kind) {
    return 1;
  }

  zlink::framework::request_call_t<int> shutdown_call (
    zlink::framework::result_t<int>::failure (
      zlink::framework::framework_error_kind_t::shutdown,
      "shutdown"));

  if (shutdown_call.submit ().result ().error_kind () !=
      zlink::framework::framework_error_kind_t::shutdown) {
    return 2;
  }

  zlink::framework::handler_registry_t handlers;
  handlers.on_request<named_handler_t, named_request_t, named_reply_t> (
    "sample",
    "topic",
    &named_handler_t::handle);
  const auto *descriptor =
    handlers.find ("sample", "topic", named_request_t::packet_name);
  if (descriptor == nullptr ||
      descriptor->packet_name != named_request_t::packet_name) {
    return 3;
  }

  return 0;
}
