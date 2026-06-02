/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/channels/channel_runtime.hpp"

#include <string>

int
main ()
{
  int retry_count = 0;
  int dead_letter_count = 0;
  std::string last_dead_letter_key;

  zlink::framework::zlink_builder_t zlink;
  zlink.max_pending (1)
    .on_retry ([&retry_count](
                 const zlink::framework::channel_reliability_event_t &) {
      ++retry_count;
    })
    .on_dead_letter (
      [&dead_letter_count, &last_dead_letter_key](
        const zlink::framework::channel_reliability_event_t &event) {
        ++dead_letter_count;
        last_dead_letter_key = event.idempotency_key;
      })
    .channel ("profile", [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client ([](zlink::framework::capability_builder_t &client) {
        client.use_discovery ();
      });
    });

  auto bus = zlink.message_bus ();
  auto runtime = zlink::framework::detail::channel_runtime_t::from (bus);
  if (bus.pending_limit () != 1 || runtime.pending_limit () != 1) {
    return 1;
  }

  auto ready_operation = runtime.queue_pending_send ("profile", "ready-key");
  if (!ready_operation || runtime.pending_count () != 1 ||
      bus.pending_count () != 1) {
    return 2;
  }

  auto retry_result = runtime.retry_pending (ready_operation.value ());
  if (!retry_result || retry_count != 1) {
    return 3;
  }

  auto full_result = runtime.queue_pending_send ("profile", "full-key");
  if (full_result ||
      full_result.error_kind () !=
        zlink::framework::framework_error_kind_t::request_rejected) {
    return 4;
  }

  auto ready_result = runtime.mark_send_ready (ready_operation.value ());
  if (!ready_result || runtime.pending_count () != 0) {
    return 5;
  }

  auto timeout_operation = runtime.queue_pending_send ("profile", "timeout-key");
  if (!timeout_operation) {
    return 6;
  }
  auto timeout_result = runtime.expire_pending (timeout_operation.value ());
  if (timeout_result ||
      timeout_result.error_kind () !=
        zlink::framework::framework_error_kind_t::timeout ||
      dead_letter_count != 1 || last_dead_letter_key != "timeout-key") {
    return 7;
  }

  auto drained_operation = runtime.queue_pending_send ("profile", "drain-key");
  if (!drained_operation) {
    return 8;
  }
  runtime.drain ();
  if (runtime.pending_count () != 0) {
    return 9;
  }

  zlink::framework::zlink_builder_t shutdown_builder;
  shutdown_builder.channel (
    "profile",
    [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client ([](zlink::framework::capability_builder_t &client) {
        client.use_discovery ();
      });
    });
  auto shutdown_runtime =
    zlink::framework::detail::channel_runtime_t::from (
      shutdown_builder.message_bus ());
  auto shutdown_pending =
    shutdown_runtime.queue_pending_send ("profile", "shutdown-key");
  if (!shutdown_pending) {
    return 10;
  }
  shutdown_runtime.shutdown ();
  if (shutdown_runtime.pending_count () != 0) {
    return 11;
  }
  auto after_shutdown =
    shutdown_runtime.queue_pending_send ("profile", "after-shutdown");
  if (after_shutdown ||
      after_shutdown.error_kind () !=
        zlink::framework::framework_error_kind_t::shutdown) {
    return 12;
  }

  zlink::framework::zlink_builder_t close_builder;
  close_builder.channel (
    "profile",
    [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client ([](zlink::framework::capability_builder_t &client) {
        client.use_discovery ();
      });
    });
  auto close_runtime = zlink::framework::detail::channel_runtime_t::from (
    close_builder.message_bus ());
  close_runtime.close ();
  auto after_close = close_runtime.reserve_outbound_request ("profile");
  if (after_close ||
      after_close.error_kind () !=
        zlink::framework::framework_error_kind_t::closed) {
    return 13;
  }

  return 0;
}
