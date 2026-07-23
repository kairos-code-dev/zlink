/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/fanout/raw_fanout_owner.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <cerrno>
#include <set>
#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::fanout
{
namespace
{

std::vector<zlink::message_t> materialize (
  const std::vector<std::vector<std::uint8_t>> &parts)
{
    std::vector<zlink::message_t> result;
    result.reserve (parts.size ());
    for (const auto &part : parts) {
        result.push_back (zlink::message_t::from (part));
    }
    return result;
}

bool submit_publish (zlink::pub_socket_t &socket,
                     const std::string &topic,
                     const std::vector<std::vector<std::uint8_t>> &parts)
{
    auto messages = materialize (parts);
    auto operation = std::move (socket.publish (topic)).message (messages[0]);
    for (std::size_t index = 1; index < messages.size (); ++index) {
        operation = std::move (operation).message (messages[index]);
    }
    return std::move (operation).submit ();
}

} // namespace

raw_fanout_publisher_t::raw_fanout_publisher_t (std::string endpoint) :
    _configured_endpoint (std::move (endpoint))
{
    if (_configured_endpoint.empty ()) {
        throw std::invalid_argument ("fanout publisher endpoint is required");
    }
}

raw_fanout_publisher_t::~raw_fanout_publisher_t () noexcept
{
    close ();
}

void raw_fanout_publisher_t::start ()
{
    std::lock_guard lock (_mutex);
    if (_socket) {
        return;
    }
    if (_closed) {
        throw std::logic_error ("fanout publisher cannot restart after close");
    }
    auto context = std::make_unique<zlink::context_t> ();
    auto socket = std::make_unique<zlink::pub_socket_t> (*context);
    socket->options ().linger (std::chrono::milliseconds (0));
    socket->bind (_configured_endpoint);
    _endpoint = socket->options ().last_endpoint ();
    _next_beacon =
      std::chrono::steady_clock::now () + fanout_beacon_interval;
    _socket = std::move (socket);
    _context = std::move (context);
}

void raw_fanout_publisher_t::close () noexcept
{
    std::unique_ptr<zlink::pub_socket_t> socket;
    std::unique_ptr<zlink::context_t> context;
    {
        std::lock_guard lock (_mutex);
        _closed = true;
        socket = std::move (_socket);
        context = std::move (_context);
    }
    if (socket) {
        try {
            socket->close ();
        }
        catch (...) {
        }
    }
    if (context) {
        try {
            context->shutdown ();
            context->term ();
        }
        catch (...) {
        }
    }
}

std::string raw_fanout_publisher_t::endpoint () const
{
    std::lock_guard lock (_mutex);
    return _endpoint;
}

bool raw_fanout_publisher_t::publish (
  const std::string &topic,
  const protocol::application_payload_t &payload)
{
    if (topic.empty () || topic == reserved_topic ()) {
        throw std::invalid_argument (
          "fanout application topic is empty or reserved");
    }
    std::lock_guard lock (_mutex);
    return _socket
           && submit_publish (
             *_socket, topic, {protocol::encode_application_payload (payload)});
}

bool raw_fanout_publisher_t::tick (
  std::chrono::steady_clock::time_point now)
{
    std::lock_guard lock (_mutex);
    if (!_socket || now < _next_beacon) {
        return false;
    }
    const auto submitted =
      submit_publish (*_socket, reserved_topic (), {beacon_payload ()});
    do {
        _next_beacon += fanout_beacon_interval;
    } while (_next_beacon <= now);
    return submitted;
}

const std::string &raw_fanout_publisher_t::reserved_topic ()
{
    static const std::string value{
      static_cast<char> (0x01), static_cast<char> (0x5a),
      static_cast<char> (0x4c), static_cast<char> (0x46),
      static_cast<char> (0x31)};
    return value;
}

const std::vector<std::uint8_t> &
raw_fanout_publisher_t::beacon_payload ()
{
    static const std::vector<std::uint8_t> value{0x5a, 0x46, 0x01, 0x01};
    return value;
}

raw_fanout_subscriber_t::raw_fanout_subscriber_t () :
    _context (std::make_unique<zlink::context_t> ())
{
}

raw_fanout_subscriber_t::~raw_fanout_subscriber_t () noexcept
{
    close ();
}

bool raw_fanout_subscriber_t::connect_manual (
  std::vector<std::uint8_t> publisher_routing_id,
  std::string endpoint)
{
    std::lock_guard lock (_mutex);
    return connect_locked (
      std::move (publisher_routing_id), std::move (endpoint), false);
}

void raw_fanout_subscriber_t::reconcile_automatic (
  const locations::service_descriptor_snapshot_t &snapshot)
{
    std::lock_guard lock (_mutex);
    if (_automatic_mode && !*_automatic_mode) {
        throw std::logic_error (
          "manual and automatic fanout subscriber modes cannot be combined");
    }
    _automatic_mode = true;
    std::set<std::vector<std::uint8_t>, byte_vector_less_t> desired;
    for (const auto &record : snapshot.records) {
        if (record.key.kind != locations::service_descriptor_kind_t::fanout
            || record.state != mesh::service_node_state_t::serving) {
            continue;
        }
        desired.insert (record.key.routing_id);
        const auto found = _connections.find (record.key.routing_id);
        if (found == _connections.end ()) {
            (void) connect_locked (
              record.key.routing_id, record.endpoint, true);
        } else if (found->second.automatic
                   && found->second.endpoint != record.endpoint) {
            found->second.socket->close ();
            found->second.endpoint = record.endpoint;
            reopen_locked (found->second);
        }
    }
    for (auto entry = _connections.begin (); entry != _connections.end ();) {
        if (entry->second.automatic
            && !desired.contains (entry->first)) {
            entry->second.socket->close ();
            entry = _connections.erase (entry);
        } else {
            ++entry;
        }
    }
}

bool raw_fanout_subscriber_t::disconnect (
  const std::vector<std::uint8_t> &publisher_routing_id)
{
    std::lock_guard lock (_mutex);
    const auto found = _connections.find (publisher_routing_id);
    if (found == _connections.end ()) {
        return false;
    }
    found->second.socket->close ();
    _connections.erase (found);
    return true;
}

void raw_fanout_subscriber_t::close () noexcept
{
    std::unique_ptr<zlink::context_t> context;
    {
        std::lock_guard lock (_mutex);
        if (_closed) {
            return;
        }
        _closed = true;
        for (auto &[id, connection] : _connections) {
            static_cast<void> (id);
            try {
                connection.socket->close ();
            }
            catch (...) {
            }
        }
        _connections.clear ();
        context = std::move (_context);
    }
    if (context) {
        try {
            context->shutdown ();
            context->term ();
        }
        catch (...) {
        }
    }
}

std::pair<fanout_receive_status_t, std::optional<fanout_received_t>>
raw_fanout_subscriber_t::try_receive (
  std::chrono::steady_clock::time_point now)
{
    std::lock_guard lock (_mutex);
    for (auto &[publisher, connection] : _connections) {
        zlink::topic_message_t message;
        const auto result =
          connection.socket->subscribe (message, zlink::recv_flags_t::dontwait);
        if (result == static_cast<int> (zlink::recv_result_t::no_data)
            || (result == -1
                && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            continue;
        }
        if (result != 0) {
            reopen_locked (connection);
            return {fanout_receive_status_t::protocol_error, std::nullopt};
        }
        const auto &parts = message.parts ();
        if (message.topic () == raw_fanout_publisher_t::reserved_topic ()) {
            if (parts.size () != 1
                || parts.front ().to_bytes ()
                     != raw_fanout_publisher_t::beacon_payload ()) {
                reopen_locked (connection);
                return {fanout_receive_status_t::protocol_error, std::nullopt};
            }
            connection.ready = true;
            connection.deadline = now + fanout_receive_deadline;
            return {fanout_receive_status_t::beacon, std::nullopt};
        }
        if (parts.size () != 1) {
            reopen_locked (connection);
            return {fanout_receive_status_t::protocol_error, std::nullopt};
        }
        try {
            auto payload =
              protocol::decode_application_payload (parts.front ().to_bytes ());
            connection.ready = true;
            connection.deadline = now + fanout_receive_deadline;
            return {
              fanout_receive_status_t::application,
              fanout_received_t{
                publisher, message.topic (), std::move (payload)}};
        }
        catch (const protocol::service_wire_error_t &) {
            reopen_locked (connection);
            return {fanout_receive_status_t::protocol_error, std::nullopt};
        }
    }
    return {fanout_receive_status_t::no_data, std::nullopt};
}

std::vector<std::vector<std::uint8_t>> raw_fanout_subscriber_t::tick (
  std::chrono::steady_clock::time_point now)
{
    std::lock_guard lock (_mutex);
    std::vector<std::vector<std::uint8_t>> timed_out;
    for (auto &[publisher, connection] : _connections) {
        if (connection.ready && now >= connection.deadline) {
            timed_out.push_back (publisher);
            reopen_locked (connection);
        }
    }
    return timed_out;
}

bool raw_fanout_subscriber_t::ready (
  const std::vector<std::uint8_t> &publisher_routing_id) const
{
    std::lock_guard lock (_mutex);
    const auto found = _connections.find (publisher_routing_id);
    return found != _connections.end () && found->second.ready;
}

std::size_t raw_fanout_subscriber_t::publisher_count () const
{
    std::lock_guard lock (_mutex);
    return _connections.size ();
}

bool raw_fanout_subscriber_t::byte_vector_less_t::operator() (
  const std::vector<std::uint8_t> &left,
  const std::vector<std::uint8_t> &right) const noexcept
{
    return std::lexicographical_compare (
      left.begin (), left.end (), right.begin (), right.end ());
}

bool raw_fanout_subscriber_t::connect_locked (
  std::vector<std::uint8_t> publisher_routing_id,
  std::string endpoint,
  bool automatic)
{
    if (_closed || !_context || publisher_routing_id.empty ()
        || endpoint.empty ()) {
        return false;
    }
    if (_automatic_mode && *_automatic_mode != automatic) {
        return false;
    }
    _automatic_mode = automatic;
    if (_connections.contains (publisher_routing_id)) {
        return false;
    }
    connection_t connection;
    connection.endpoint = std::move (endpoint);
    connection.automatic = automatic;
    reopen_locked (connection);
    _connections.emplace (
      std::move (publisher_routing_id), std::move (connection));
    return true;
}

void raw_fanout_subscriber_t::reopen_locked (connection_t &connection)
{
    if (connection.socket) {
        connection.socket->close ();
    }
    auto socket = std::make_unique<zlink::sub_socket_t> (*_context);
    socket->options ().linger (std::chrono::milliseconds (0));
    socket->set_subscription ("");
    socket->connect (connection.endpoint);
    connection.socket = std::move (socket);
    connection.ready = false;
    connection.deadline = {};
}

} // namespace zlink::framework::runtime::fanout
