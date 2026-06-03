/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/contracts/connector.hpp>

#include "runtime/transport/transport_connection.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace zlink::stream_connector::detail
{

struct pending_request_t
{
  std::uint64_t request_seq = 0;
  packet_t packet;
};

class connector_state_t
{
public:
  explicit connector_state_t (connector_options_t options)
    : options (std::move (options))
  {
  }

  connector_options_t options;
  connection_state_t state = connection_state_t::created;
  std::uint64_t next_request_seq = 1;
  std::map<std::uint64_t, pending_request_t> pending_requests;
  std::deque<packet_t> dispatch_queue;
  std::vector<packet_t> sent_packets;
  std::map<std::string, std::vector<std::function<void (const packet_t &)>>>
    packet_handlers;
  std::vector<std::function<void (const connection_state_changed_t &)>>
    state_handlers;
  std::vector<std::function<void (const error_t &)>> error_handlers;
  std::vector<std::function<void ()>> disconnected_handlers;
  std::map<std::type_index, codec_t> codecs;
  bool json_enabled = true;
  bool message_pack_enabled = false;
  bool protobuf_enabled = false;
  bool lz4_enabled = false;
  std::chrono::steady_clock::time_point last_heartbeat_sent {};
  std::chrono::steady_clock::time_point last_inbound_received {};
  boost::asio::io_context io_context;
  std::unique_ptr<stream_connection_t> connection;
  std::mutex transport_mutex;
};

class connector_runtime_t
{
public:
  explicit connector_runtime_t (std::shared_ptr<connector_state_t> state);

  static connector_runtime_t from (const connector_t &connector);

  void receive_packet (packet_t packet);
  const std::vector<packet_t> &sent_packets () const noexcept;
  std::size_t pending_request_count () const noexcept;

private:
  std::shared_ptr<connector_state_t> _state;
};

result_t<void> submit_send (std::shared_ptr<connector_state_t> state,
                            packet_t packet);
result_t<void> dispatch_pending (std::shared_ptr<connector_state_t> state);
void deliver_received_packet (connector_state_t &state, packet_t packet);
void change_state (std::shared_ptr<connector_state_t> state,
                   connection_state_t next,
                   std::optional<error_t> error = std::nullopt);

} // namespace zlink::stream_connector::detail
