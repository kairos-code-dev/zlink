/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/streams/stream.hpp>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

class stream_builder_state_t
{
  public:
    explicit stream_builder_state_t (std::string name) : snapshot{.name = std::move (name)} {}

    stream_snapshot_t snapshot;
};

class stream_state_t
{
  public:
    std::string session_id;
    bool closed = false;
    std::deque<std::string> serial_log;
    std::vector<stream_header_t> written_headers;
    std::vector<zlink::message_t> written_payloads;
};

class stream_runtime_state_t
{
  public:
    std::map<std::string, std::shared_ptr<stream_builder_state_t>> streams;
    std::uint64_t next_session_id = 1;
};

class stream_runtime_t
{
  public:
    explicit stream_runtime_t (std::shared_ptr<stream_runtime_state_t> state);

    static stream_runtime_t from (const zlink_builder_t &builder);

    result_t<std::vector<std::uint8_t>> encode_header (const stream_header_t &header) const;
    result_t<stream_header_t> decode_header (const std::vector<std::uint8_t> &bytes) const;
    result_t<void> validate_header (const stream_header_t &header) const;

    stream_t open_session (std::string stream_name) const;
    result_t<void> dispatch_connected (packet_stream_session_t &session, stream_t &stream) const;
    result_t<void> dispatch_packet (packet_stream_session_t &session,
                                    stream_t &stream,
                                    const stream_header_t &header,
                                    const zlink::message_t &payload) const;
    result_t<void> dispatch_disconnected (packet_stream_session_t &session, stream_t &stream) const;
    result_t<void> dispatch_error (packet_stream_session_t &session,
                                   stream_t &stream,
                                   const stream_error_t &error) const;

    std::vector<std::string> serial_log (const stream_t &stream) const;
    std::vector<stream_header_t> written_headers (const stream_t &stream) const;

  private:
    result_t<void> dispatch_serial (stream_t &stream,
                                    std::string operation,
                                    std::function<task_t<void> ()> callback) const;

    std::shared_ptr<stream_runtime_state_t> _state;
};

} // namespace zlink::framework::detail
