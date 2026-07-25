/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/stateful/public_host_runtime.hpp"
#include "runtime/locations/pending_creation_projection.hpp"
#include "runtime/locations/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::host
{
namespace
{

constexpr std::string_view multipart_content_type =
  "application/x-zlink-framework-multipart";
bool user_spot_operation_replay_expired (
  std::uint64_t deadline_unix_ms,
  std::int64_t now_unix_ms,
  std::chrono::milliseconds replay_retention)
{
    return now_unix_ms >= 0
      && static_cast<std::uint64_t> (now_unix_ms) > deadline_unix_ms
      && static_cast<std::uint64_t> (now_unix_ms) - deadline_unix_ms
           > static_cast<std::uint64_t> (
             std::max<std::int64_t> (0, replay_retention.count ()));
}

void append_u32 (std::vector<std::uint8_t> &out, std::uint32_t value)
{
    out.push_back (static_cast<std::uint8_t> ((value >> 24u) & 0xffu));
    out.push_back (static_cast<std::uint8_t> ((value >> 16u) & 0xffu));
    out.push_back (static_cast<std::uint8_t> ((value >> 8u) & 0xffu));
    out.push_back (static_cast<std::uint8_t> (value & 0xffu));
}

std::uint32_t read_u32 (const std::vector<std::uint8_t> &bytes,
                        std::size_t &offset)
{
    if (offset + 4 > bytes.size ()) {
        throw protocol::service_wire_error_t (
          "framework multipart payload is truncated");
    }
    const auto value =
      (static_cast<std::uint32_t> (bytes[offset]) << 24u)
      | (static_cast<std::uint32_t> (bytes[offset + 1]) << 16u)
      | (static_cast<std::uint32_t> (bytes[offset + 2]) << 8u)
      | static_cast<std::uint32_t> (bytes[offset + 3]);
    offset += 4;
    return value;
}

std::vector<std::uint8_t> encode_parts (
  const std::vector<zlink::message_t> &parts)
{
    if (parts.size () > std::numeric_limits<std::uint32_t>::max ()) {
        throw std::length_error ("framework multipart part count is too large");
    }
    std::vector<std::uint8_t> encoded;
    append_u32 (encoded, static_cast<std::uint32_t> (parts.size ()));
    for (const auto &part : parts) {
        const auto bytes = part.to_bytes ();
        if (bytes.size () > std::numeric_limits<std::uint32_t>::max ()) {
            throw std::length_error ("framework multipart part is too large");
        }
        append_u32 (encoded, static_cast<std::uint32_t> (bytes.size ()));
        encoded.insert (encoded.end (), bytes.begin (), bytes.end ());
    }
    return encoded;
}

std::vector<zlink::message_t> decode_parts (
  const std::vector<std::uint8_t> &encoded)
{
    std::size_t offset = 0;
    const auto count = read_u32 (encoded, offset);
    std::vector<zlink::message_t> parts;
    parts.reserve (count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto size = read_u32 (encoded, offset);
        if (offset + size > encoded.size ()) {
            throw protocol::service_wire_error_t (
              "framework multipart part is truncated");
        }
        parts.push_back (zlink::message_t::from (
          std::span<const std::uint8_t> (encoded.data () + offset, size)));
        offset += size;
    }
    if (offset != encoded.size ()) {
        throw protocol::service_wire_error_t (
          "framework multipart payload has trailing bytes");
    }
    return parts;
}

zlink::submit_result_t submitted (bool accepted)
{
    return accepted ? zlink::submit_result_t::ok
                    : zlink::submit_result_t::not_connected;
}

record_kind_t record_kind (protocol::command command)
{
    switch (command) {
        case protocol::command::nodeSend:
            return record_kind_t::node_send;
        case protocol::command::nodeRequest:
            return record_kind_t::node_request;
        case protocol::command::channelSend:
            return record_kind_t::channel_send;
        case protocol::command::channelRequest:
            return record_kind_t::channel_request;
        case protocol::command::spotSend:
            return record_kind_t::spot_send;
        case protocol::command::spotRequest:
            return record_kind_t::spot_request;
        case protocol::command::actorSend:
            return record_kind_t::actor_send;
        case protocol::command::actorRequest:
            return record_kind_t::actor_request;
        default:
            throw protocol::service_wire_error_t (
              "mailbox record is not application messaging");
    }
}

operation_kind_t operation_kind (record_kind_t)
{
    return operation_kind_t::none;
}

bool is_request (record_kind_t kind)
{
    return kind == record_kind_t::node_request
           || kind == record_kind_t::channel_request
           || kind == record_kind_t::spot_request
           || kind == record_kind_t::actor_request;
}

std::vector<std::vector<std::uint8_t>>
unpack_infrastructure_reply (const std::vector<std::uint8_t> &packed)
{
    if (packed.empty () || packed.front () == 0
        || packed.front () > 2) {
        throw protocol::service_wire_error_t (
          "invalid packed infrastructure reply");
    }
    std::size_t offset = 1;
    std::vector<std::vector<std::uint8_t>> parts;
    parts.reserve (packed.front ());
    for (std::uint8_t index = 0; index < packed.front (); ++index) {
        const auto length = read_u32 (packed, offset);
        if (packed.size () - offset < length) {
            throw protocol::service_wire_error_t (
              "truncated packed infrastructure reply");
        }
        parts.emplace_back (
          packed.begin () + static_cast<std::ptrdiff_t> (offset),
          packed.begin ()
            + static_cast<std::ptrdiff_t> (offset + length));
        offset += length;
    }
    if (offset != packed.size ()) {
        throw protocol::service_wire_error_t (
          "packed infrastructure reply has trailing bytes");
    }
    return parts;
}

std::string user_spot_operation_key (
  const std::vector<std::uint8_t> &source,
  std::uint64_t source_generation,
  const protocol::wire_operation_id_t &operation)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill ('0');
    for (const auto value : source)
        stream << std::setw (2) << static_cast<unsigned> (value);
    stream << ':' << source_generation << ':' << operation.high
           << ':' << operation.low;
    return stream.str ();
}

std::vector<std::byte> ready_user_spot_authority_payload (
  const stateful::object_ref_t &object,
  const std::string &stable_type)
{
    // The authority payload is framework-owned. Application creation bytes
    // remain in the reservation projection and are never published as Ready.
    const std::string value =
      "zlink:user-spot:ready:v1\n" + stable_type + "\n"
      + object.key + "\n" + std::to_string (object.object_generation)
      + "\n"
      + std::to_string (object.authority_owner_generation);
    std::vector<std::byte> result;
    result.reserve (value.size ());
    for (const auto character : value)
        result.push_back (
          static_cast<std::byte> (
            static_cast<unsigned char> (character)));
    return result;
}

std::vector<std::byte> closing_user_spot_authority_payload (
  const stateful::object_ref_t &object)
{
    const std::string value =
      "zlink:user-spot:closing:v1\n" + object.key + "\n"
      + std::to_string (object.object_generation) + "\n"
      + std::to_string (object.authority_owner_generation);
    std::vector<std::byte> result;
    result.reserve (value.size ());
    for (const auto character : value)
        result.push_back (
          static_cast<std::byte> (
            static_cast<unsigned char> (character)));
    return result;
}

std::uint64_t unix_milliseconds_now ()
{
    return static_cast<std::uint64_t> (
      std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::system_clock::now ().time_since_epoch ())
        .count ());
}

zlink::framework::object_reservation_fence_t public_fence (
  const protocol::user_spot_reservation_fence_t &wire,
  const std::string &mesh_name,
  const std::string &stable_type)
{
    if (wire.target_owner_lease_generation
        > static_cast<std::uint64_t> (
          std::numeric_limits<std::int64_t>::max ()))
        throw protocol::service_wire_error_t (
          "target owner lease generation exceeds the public Store range");
    return {
      wire.reservation_id,
      wire.expected_store_version,
      wire.object_generation,
      wire.authority_owner_generation,
      {mesh_name,
       node_rid_t::from_string (
         zlink::routing_id_t::from (
           wire.target_node_routing_id)
           .to_string ()),
       wire.target_node_generation,
       {wire.target_owner_id,
        static_cast<std::int64_t> (
          wire.target_owner_lease_generation)}},
      {0,
       wire.pending_capacity_delta,
       spot_type_capacity_delta_t{
         placement_object_kind_t::user_spot,
         stable_type,
         wire.pending_capacity_delta}}};
}

} // namespace

zlink::routing_id_t node_status_t::routing_id () const
{
    return node_routing_id;
}

std::string node_status_t::local_endpoint () const
{
    return endpoint;
}

std::uint64_t node_status_t::lifecycle_generation () const noexcept
{
    return generation;
}

std::uint64_t spot_status_t::lifecycle_generation () const noexcept
{
    return generation;
}

spot_handle_t::spot_handle_t (
  std::shared_ptr<public_host_runtime_t> host,
  stateful::object_ref_t object) :
    _host (std::move (host)), _object (std::move (object))
{
}

spot_status_t spot_handle_t::status () const
{
    return {_object.object_generation};
}

const std::string &spot_handle_t::spot_id () const noexcept
{
    return _object.key;
}

zlink::submit_result_t spot_handle_t::send_to_spot (
  const zlink::routing_id_t &target_node_rid,
  const std::string &target_spot_id,
  std::uint64_t target_spot_generation,
  const std::vector<zlink::message_t> &parts,
  zlink::send_flags_t,
  std::span<const std::uint8_t> metadata)
{
    if (!_host) {
        return zlink::submit_result_t::invalid_handle;
    }
    const auto peer = _host->transport ().topology ().peer (
      target_node_rid.to_bytes ());
    const auto target_node_generation =
      peer ? peer->descriptor.lifecycle_generation
           : _host->status ().lifecycle_generation ();
    const auto target = protocol::spot_route_fence_t{
      target_spot_id,
      target_spot_generation,
      target_node_rid.to_bytes (),
      target_node_generation,
      target_spot_generation};
    return submitted (_host->transport ().send_to_spot (
      target_node_rid.to_bytes (), spot_id (), target,
      _host->encode_application (parts, metadata)));
}

zlink::submit_result_t spot_handle_t::request_to_spot (
  const zlink::routing_id_t &target_node_rid,
  const std::string &target_spot_id,
  std::uint64_t target_spot_generation,
  const std::vector<zlink::message_t> &parts,
  operation_id_t &operation,
  zlink::send_flags_t,
  std::chrono::milliseconds timeout,
  std::span<const std::uint8_t> metadata)
{
    if (!_host) {
        return zlink::submit_result_t::invalid_handle;
    }
    operation = _host->next_operation ();
    const auto peer = _host->transport ().topology ().peer (
      target_node_rid.to_bytes ());
    const auto target_node_generation =
      peer ? peer->descriptor.lifecycle_generation
           : _host->status ().lifecycle_generation ();
    const auto target = protocol::spot_route_fence_t{
      target_spot_id,
      target_spot_generation,
      target_node_rid.to_bytes (),
      target_node_generation,
      target_spot_generation};
    const auto host = _host;
    return submitted (_host->transport ().request_to_spot (
      target_node_rid.to_bytes (), spot_id (), target,
      _host->encode_application (parts, metadata), timeout,
      [host, operation] (foundation::operation_terminal_t terminal,
                         std::vector<std::uint8_t> payload) mutable {
          host->complete_operation (
            operation, operation_kind_t::none, terminal,
            std::move (payload));
      }));
}

zlink::submit_result_t spot_handle_t::publish (
  const std::string &channel_name,
  const std::string &,
  const std::vector<zlink::message_t> &parts,
  zlink::send_flags_t,
  std::span<const std::uint8_t> metadata)
{
    if (!_host)
        return zlink::submit_result_t::invalid_handle;
    const auto targets =
      _host->transport ().topology ().multicast_targets (
        channel_name);
    const auto encoded =
      _host->encode_application (parts, metadata);
    for (const auto &target : targets) {
        (void) _host->transport ().send_to_node (
          target.descriptor.node_routing_id,
          encoded);
    }
    return zlink::submit_result_t::ok;
}

void spot_handle_t::set_subscription (const std::string &,
                                      const std::string &)
{
}

void spot_handle_t::unset_subscription (const std::string &,
                                        const std::string &)
{
}

bool spot_handle_t::close () noexcept
{
    if (!_host) {
        return false;
    }
    const auto [error, closed] = _host->objects ().close_spot (_object);
    return error == stateful::stateful_error_t::none && closed;
}

actor_handle_t::actor_handle_t (
  std::shared_ptr<public_host_runtime_t> host,
  actor_ref_t actor,
  stateful::object_ref_t object) :
    _host (std::move (host)),
    _actor (std::move (actor)),
    _object (std::move (object))
{
}

const actor_ref_t &actor_handle_t::ref () const noexcept
{
    return _actor;
}

zlink::submit_result_t actor_handle_t::join_entry_spot (
  const zlink::routing_id_t &target_node_rid,
  const std::vector<zlink::message_t> &parts,
  operation_id_t &operation,
  std::chrono::milliseconds timeout)
{
    if (!_host) {
        return zlink::submit_result_t::invalid_handle;
    }
    const auto entry = _host->entry_spot ();
    return join_spot (
      target_node_rid, entry.spot_id (),
      entry.status ().lifecycle_generation (), parts, operation, timeout);
}

zlink::submit_result_t actor_handle_t::join_spot (
  const zlink::routing_id_t &target_node_rid,
  const std::string &target_spot_id,
  std::uint64_t target_spot_generation,
  const std::vector<zlink::message_t> &parts,
  operation_id_t &operation,
  std::chrono::milliseconds)
{
    if (!_host) {
        return zlink::submit_result_t::invalid_handle;
    }
    if (target_node_rid.to_bytes ()
        != _host->status ().routing_id ().to_bytes ()) {
        return zlink::submit_result_t::not_connected;
    }
    return _host->begin_local_actor_join (
      _actor, target_spot_id, target_spot_generation, parts, operation);
}

zlink::submit_result_t actor_handle_t::send_to (
  const actor_ref_t &target,
  const std::vector<zlink::message_t> &parts,
  zlink::send_flags_t,
  std::span<const std::uint8_t> metadata)
{
    return !_host ? zlink::submit_result_t::invalid_handle
                  : _host->send_to_actor (target, parts, metadata);
}

zlink::submit_result_t actor_handle_t::request_to (
  const actor_ref_t &target,
  const std::vector<zlink::message_t> &parts,
  operation_id_t &operation,
  zlink::send_flags_t,
  std::chrono::milliseconds timeout,
  std::span<const std::uint8_t> metadata)
{
    return !_host ? zlink::submit_result_t::invalid_handle
                  : _host->request_to_actor (
                      target, parts, operation, timeout, metadata);
}

public_host_runtime_t::public_host_runtime_t (host_options_t options) :
    _options (std::move (options)),
    _transport (
      std::make_shared<mesh::raw_mesh_node_owner_t> (_options.mesh)),
    _sessions ([this] (const std::string &actor_id) {
        std::lock_guard lock (_mutex);
        const auto found = _actors.find (actor_id);
        return found == _actors.end ()
                 ? std::optional<stateful::object_ref_t>{}
                 : std::make_optional (found->second.second);
    })
{
    const auto &descriptor = _options.mesh.descriptor;
    _objects.replace_placement_candidates (
      {stateful::placement_candidate_t{
        descriptor.mesh_name,
        std::string (descriptor.node_routing_id.begin (),
                     descriptor.node_routing_id.end ()),
        _options.object_stable_types,
        descriptor.placement_weight,
        descriptor.active_capacity_limit,
        descriptor.active_capacity_used,
        descriptor.pending_capacity_limit,
        descriptor.pending_capacity_used}});
}

public_host_runtime_t::~public_host_runtime_t ()
{
    close ();
}

void public_host_runtime_t::start ()
{
    std::lock_guard lock (_mutex);
    if (_started) {
        return;
    }
    _transport->start ();
    _started = true;
    if (_maintenance_started)
        _maintenance_started ();
}

void public_host_runtime_t::close () noexcept
{
    std::function<void ()> maintenance_closing;
    {
        std::lock_guard lock (_mutex);
        if (!_started) {
            return;
        }
        maintenance_closing = _maintenance_closing;
    }
    if (maintenance_closing) {
        try {
            maintenance_closing ();
        }
        catch (...) {
        }
    }
    {
        std::lock_guard lock (_mutex);
        _started = false;
        _completions.clear ();
    }
    _transport->close ();
}

bool public_host_runtime_t::connect_peer (
  const std::string &endpoint,
  std::optional<zlink::routing_id_t> expected)
{
    bool connected = false;
    if (expected) {
        auto descriptor = _options.mesh.descriptor;
        descriptor.node_routing_id = expected->to_bytes ();
        descriptor.advertised_endpoint = endpoint;
        connected = _transport->connect_peer (
          endpoint, std::move (descriptor));
    } else {
        connected = _transport->connect_peer (endpoint);
    }
    if (connected) {
        std::lock_guard lock (_mutex);
        _peer_endpoints.insert_or_assign (
          endpoint, expected ? expected->to_string () : std::string{});
    }
    return connected;
}

void public_host_runtime_t::disconnect_peer (
  const std::string &endpoint) noexcept
{
    std::lock_guard lock (_mutex);
    _peer_endpoints.erase (endpoint);
}

node_status_t public_host_runtime_t::status () const
{
    const auto descriptor = _transport->topology ().local_descriptor ();
    node_status_t::state_t state = node_status_t::state_t::preparing;
    switch (descriptor.state) {
        case mesh::service_node_state_t::serving:
            state = node_status_t::state_t::serving;
            break;
        case mesh::service_node_state_t::draining:
        case mesh::service_node_state_t::retiring:
            state = node_status_t::state_t::draining;
            break;
        case mesh::service_node_state_t::stopped:
            state = node_status_t::state_t::stopped;
            break;
        case mesh::service_node_state_t::error:
            state = node_status_t::state_t::error;
            break;
        default:
            break;
    }
    return {state,
            zlink::routing_id_t::from (descriptor.node_routing_id),
            descriptor.advertised_endpoint,
            descriptor.lifecycle_generation};
}

void public_host_runtime_t::set_channel_weight (
  const std::string &channel_name,
  std::uint32_t weight)
{
    if (weight > 10000) {
        throw std::invalid_argument ("channel weight exceeds 10000");
    }
    auto descriptor = _transport->topology ().local_descriptor ();
    const auto found = std::find_if (
      descriptor.channels.begin (), descriptor.channels.end (),
      [&] (const auto &channel) { return channel.name == channel_name; });
    if (found == descriptor.channels.end ()) {
        throw std::invalid_argument ("channel is not registered");
    }
    found->weight = weight;
    ++descriptor.descriptor_revision;
    _transport->topology ().publish_local (std::move (descriptor));
}

std::int64_t public_host_runtime_t::max_message_size () const
{
    return _transport->topology ()
      .local_descriptor ()
      .effective_max_message_bytes;
}

void public_host_runtime_t::set_max_message_size (std::int64_t value)
{
    if (value <= 0
        || value > std::numeric_limits<std::uint32_t>::max ()) {
        throw std::invalid_argument (
          "max message size is outside the raw service range");
    }
    auto descriptor = _transport->topology ().local_descriptor ();
    descriptor.effective_max_message_bytes =
      static_cast<std::uint32_t> (value);
    ++descriptor.descriptor_revision;
    _transport->topology ().publish_local (std::move (descriptor));
}

mesh::raw_mesh_node_owner_t &public_host_runtime_t::transport () noexcept
{
    return *_transport;
}

stateful::stateful_object_runtime_t &
public_host_runtime_t::objects () noexcept
{
    return _objects;
}

stateful::stream_session_registry_t &
public_host_runtime_t::sessions () noexcept
{
    return _sessions;
}

void public_host_runtime_t::configure_user_spot_operations (
  std::shared_ptr<zlink::framework::location_store_t> store,
  user_spot_materializer_t materializer)
{
    if (!store || !materializer)
        throw std::invalid_argument (
          "User Spot operations require a Location Store and materializer");
    std::lock_guard lock (_mutex);
    if (_started)
        throw std::logic_error (
          "User Spot operations must be configured before start");
    _user_spot_store = std::move (store);
    _user_spot_materializer = std::move (materializer);
}

bool public_host_runtime_t::create_user_spot_remote (
  const zlink::routing_id_t &target_node,
  protocol::user_spot_create_header_t request,
  std::chrono::milliseconds timeout,
  user_spot_create_completion_t completion)
{
    if (!completion)
        throw std::invalid_argument (
          "User Spot create completion is required");
    return _transport->request_user_spot_create (
      target_node.to_bytes (), std::move (request), timeout,
      [completion = std::move (completion)] (
        foundation::operation_terminal_t terminal,
        std::vector<std::uint8_t> packed) mutable {
          protocol::user_spot_create_reply_t reply;
          std::optional<protocol::application_payload_t>
            application_reply;
          if (terminal
              == foundation::operation_terminal_t::completed) {
              try {
                  const auto parts =
                    unpack_infrastructure_reply (packed);
                  reply =
                    protocol::decode_user_spot_create_reply (
                      parts.front ());
                  if (parts.size () == 2)
                      application_reply =
                        protocol::decode_application_payload (parts[1]);
              }
              catch (const protocol::service_wire_error_t &) {
                  terminal =
                    foundation::operation_terminal_t::transport_failed;
              }
          }
          completion (
            terminal, std::move (reply),
            std::move (application_reply));
      });
}

bool public_host_runtime_t::close_user_spot_remote (
  const zlink::routing_id_t &target_node,
  protocol::user_spot_close_header_t request,
  std::chrono::milliseconds timeout,
  user_spot_close_completion_t completion)
{
    if (!completion)
        throw std::invalid_argument (
          "User Spot close completion is required");
    return _transport->request_user_spot_close (
      target_node.to_bytes (), std::move (request), timeout,
      [completion = std::move (completion)] (
        foundation::operation_terminal_t terminal,
        std::vector<std::uint8_t> packed) mutable {
          protocol::user_spot_close_reply_t reply;
          if (terminal
              == foundation::operation_terminal_t::completed) {
              try {
                  const auto parts =
                    unpack_infrastructure_reply (packed);
                  if (parts.size () != 1)
                      throw protocol::service_wire_error_t (
                        "User Spot close reply carries a payload");
                  reply =
                    protocol::decode_user_spot_close_reply (
                      parts.front ());
              }
              catch (const protocol::service_wire_error_t &) {
                  terminal =
                    foundation::operation_terminal_t::transport_failed;
              }
          }
          completion (terminal, std::move (reply));
      });
}

spot_handle_t public_host_runtime_t::entry_spot ()
{
    return get_or_create_spot (
      status ().routing_id ().to_string () + "-entry-" + _options.entry_spot_name);
}

spot_handle_t public_host_runtime_t::get_or_create_spot (
  std::string spot_id)
{
    const auto &key = spot_id;
    {
        std::lock_guard lock (_mutex);
        const auto found = _spots.find (key);
        if (found != _spots.end ()) {
            return spot_handle_t (shared_from_this (), found->second);
        }
    }
    auto created = _objects.begin_create (
      stateful::create_request_t{
        stateful::object_kind_t::user_spot,
        key,
        "framework.spot",
        _options.mesh.descriptor.mesh_name,
        {},
        false,
        false});
    if (created.error != stateful::stateful_error_t::none) {
        throw std::runtime_error ("framework Spot authority creation failed");
    }
    if (created.factory_owner
        && _objects.commit_create (created.attempt)
             != stateful::stateful_error_t::none) {
        throw std::runtime_error ("framework Spot Ready commit failed");
    }
    auto object = _objects.find (stateful::object_kind_t::user_spot, key);
    if (!object) {
        throw std::runtime_error ("framework Spot authority is unavailable");
    }
    {
        std::lock_guard lock (_mutex);
        _spots.insert_or_assign (key, *object);
    }
    return spot_handle_t (shared_from_this (), *object);
}

actor_handle_t public_host_runtime_t::create_actor (
  std::string actor_type,
  std::string actor_id)
{
    {
        std::lock_guard lock (_mutex);
        const auto found = _actors.find (actor_id);
        if (found != _actors.end ()) {
            return actor_handle_t (
              shared_from_this (),
              framework_actor_ref (found->second.second, found->second.first),
              found->second.second);
        }
    }
    auto created = _objects.begin_create (
      stateful::create_request_t{
        stateful::object_kind_t::actor,
        actor_id,
        actor_type,
        _options.mesh.descriptor.mesh_name,
        {},
        false,
        false});
    if (created.error != stateful::stateful_error_t::none) {
        throw std::runtime_error ("framework Actor authority creation failed");
    }
    if (created.factory_owner
        && _objects.commit_create (created.attempt)
             != stateful::stateful_error_t::none) {
        throw std::runtime_error ("framework Actor Ready commit failed");
    }
    auto object = _objects.find (stateful::object_kind_t::actor, actor_id);
    if (!object) {
        throw std::runtime_error ("framework Actor authority is unavailable");
    }
    {
        std::lock_guard lock (_mutex);
        _actors.insert_or_assign (
          actor_id, std::make_pair (actor_type, *object));
    }
    return actor_handle_t (
      shared_from_this (), framework_actor_ref (*object, actor_type), *object);
}

zlink::submit_result_t public_host_runtime_t::send_to_actor (
  const actor_ref_t &target,
  const std::vector<zlink::message_t> &parts,
  std::span<const std::uint8_t> metadata)
{
    const auto target_routing_id =
      zlink::routing_id_t::from (
        std::string (target.node_rid ().value ()));
    if (target_routing_id.to_bytes ()
          == status ().routing_id ().to_bytes ()
        && resolve_actor (target)) {
        return submitted (enqueue_local_actor_message (
          target, record_kind_t::actor_send, parts));
    }
    const auto peer = _transport->topology ().peer (
      target_routing_id.to_bytes ());
    const auto node_generation =
      peer ? peer->descriptor.lifecycle_generation
           : status ().lifecycle_generation ();
    const auto object = resolve_actor (target);
    const auto authority_generation =
      object ? object->authority_owner_generation : target.generation ();
    return submitted (_transport->send_to_actor (
      zlink::routing_id_t::from (
        std::string (target.node_rid ().value ())).to_bytes (), std::nullopt,
      protocol::actor_route_fence_t{
        std::string (target.actor_id ()),
        target.generation (),
        zlink::routing_id_t::from (
          std::string (target.node_rid ().value ())).to_bytes (),
        node_generation,
        authority_generation},
      encode_application (parts, metadata)));
}

zlink::submit_result_t public_host_runtime_t::request_to_actor (
  const actor_ref_t &target,
  const std::vector<zlink::message_t> &parts,
  operation_id_t &operation,
  std::chrono::milliseconds timeout,
  std::span<const std::uint8_t> metadata)
{
    operation = next_operation ();
    const auto target_routing_id =
      zlink::routing_id_t::from (
        std::string (target.node_rid ().value ()));
    if (target_routing_id.to_bytes ()
          == status ().routing_id ().to_bytes ()
        && resolve_actor (target)) {
        return submitted (enqueue_local_actor_message (
          target, record_kind_t::actor_request, parts, operation));
    }
    const auto peer = _transport->topology ().peer (
      target_routing_id.to_bytes ());
    const auto node_generation =
      peer ? peer->descriptor.lifecycle_generation
           : status ().lifecycle_generation ();
    const auto object = resolve_actor (target);
    const auto authority_generation =
      object ? object->authority_owner_generation : target.generation ();
    const auto host = shared_from_this ();
    return submitted (_transport->request_to_actor (
      zlink::routing_id_t::from (
        std::string (target.node_rid ().value ())).to_bytes (), std::nullopt,
      protocol::actor_route_fence_t{
        std::string (target.actor_id ()),
        target.generation (),
        zlink::routing_id_t::from (
          std::string (target.node_rid ().value ())).to_bytes (),
        node_generation,
        authority_generation},
      encode_application (parts, metadata), timeout,
      [host, operation] (foundation::operation_terminal_t terminal,
                         std::vector<std::uint8_t> payload) mutable {
          host->complete_operation (
            operation, operation_kind_t::none, terminal,
            std::move (payload));
      }));
}

zlink::submit_result_t public_host_runtime_t::send_to_node (
  const zlink::routing_id_t &target,
  const std::vector<zlink::message_t> &parts)
{
    return submitted (_transport->send_to_node (
      target.to_bytes (), encode_application (parts)));
}

zlink::submit_result_t public_host_runtime_t::request_to_node (
  const zlink::routing_id_t &target,
  const std::vector<zlink::message_t> &parts,
  operation_id_t &operation,
  std::chrono::milliseconds timeout)
{
    operation = next_operation ();
    const auto host = shared_from_this ();
    return submitted (_transport->request_to_node (
      target.to_bytes (), encode_application (parts), timeout,
      [host, operation] (foundation::operation_terminal_t terminal,
                         std::vector<std::uint8_t> payload) mutable {
          host->complete_operation (
            operation, operation_kind_t::none, terminal,
            std::move (payload));
      }));
}

zlink::submit_result_t public_host_runtime_t::send_to_channel (
  const std::string &channel_name,
  const std::vector<zlink::message_t> &parts)
{
    return submitted (_transport->send_to_channel (
      channel_name, encode_application (parts)));
}

zlink::submit_result_t public_host_runtime_t::request_to_channel (
  const std::string &channel_name,
  const std::vector<zlink::message_t> &parts,
  operation_id_t &operation,
  std::chrono::milliseconds timeout)
{
    operation = next_operation ();
    const auto host = shared_from_this ();
    return submitted (_transport->request_to_channel (
      channel_name, encode_application (parts), timeout,
      [host, operation] (foundation::operation_terminal_t terminal,
                         std::vector<std::uint8_t> payload) mutable {
          host->complete_operation (
            operation, operation_kind_t::none, terminal,
            std::move (payload));
      }));
}

std::size_t public_host_runtime_t::dispatch_user_spot_operations ()
{
    std::shared_ptr<zlink::framework::location_store_t> store;
    user_spot_materializer_t materializer;
    {
        std::lock_guard lock (_mutex);
        store = _user_spot_store;
        materializer = _user_spot_materializer;
    }
    std::size_t count = 0;
    while (auto claim = _transport->mailbox ().try_claim (
             mesh::service_mailbox_domain_t::infrastructure, 64,
             4u * 1024u * 1024u)) {
        for (auto &mailbox_record : claim->records) {
            ++count;
            try {
                const auto wire =
                  protocol::decode_header (
                    mailbox_record.parts.front ());
                if (wire.kind != protocol::command::userSpotCreate
                    && wire.kind
                         != protocol::command::userSpotClose)
                    throw protocol::service_wire_error_t (
                      "unsupported infrastructure mailbox command");

                if (wire.kind
                    == protocol::command::userSpotCreate) {
                    const auto request =
                      protocol::decode_user_spot_create_header (
                        mailbox_record.parts.front ());
                    auto fingerprint_request = request;
                    fingerprint_request.correlation = 1;
                    const auto request_fingerprint =
                      protocol::encode_user_spot_create_header (
                        fingerprint_request);
                    const auto operation_key =
                      user_spot_operation_key (
                        request.source_node_routing_id,
                        request.source_node_generation,
                        request.operation);
                    std::optional<user_spot_terminal_record_t>
                      cached;
                    {
                        std::lock_guard lock (_mutex);
                        const auto found =
                          _user_spot_terminals.find (
                            operation_key);
                        if (found != _user_spot_terminals.end ()) {
                            if (user_spot_operation_replay_expired (
                                  found->second.deadline_unix_ms,
                                  unix_milliseconds_now (),
                                  _options
                                    .user_spot_operation_replay_retention))
                                _user_spot_terminals.erase (found);
                            else
                                cached = found->second;
                        }
                    }
                    if (cached) {
                        if (cached->kind
                              != protocol::command::userSpotCreate
                            || cached->request_fingerprint
                                 != request_fingerprint)
                            throw protocol::service_wire_error_t (
                              "user spot operation identity was reused with a different request");
                        auto reply =
                          protocol::decode_user_spot_create_reply (
                            cached->header);
                        reply.header.correlation =
                          request.correlation;
                        (void) _transport
                          ->reply_user_spot_create (
                            mailbox_record, reply,
                            cached->application_reply);
                        continue;
                    }
                    {
                        std::lock_guard lock (_mutex);
                        if (_user_spot_terminals.size ()
                            >= _options.user_spot_operation_capacity) {
                            const auto now = unix_milliseconds_now ();
                            std::erase_if (
                              _user_spot_terminals,
                              [this, now] (const auto &entry) {
                                  return user_spot_operation_replay_expired (
                                    entry.second.deadline_unix_ms, now,
                                    _options
                                      .user_spot_operation_replay_retention);
                              });
                        }
                        if (_user_spot_terminals.size ()
                            >= _options.user_spot_operation_capacity) {
                            protocol::user_spot_create_reply_t reply{
                              {request.correlation, 103, 0},
                              protocol::user_spot_create_result_t::rejected,
                              {},
                              0};
                            (void) _transport->reply_user_spot_create (
                              mailbox_record, reply, std::nullopt);
                            continue;
                        }
                    }
                    auto terminal =
                      [&] (std::uint32_t terminal_result,
                           std::uint32_t failure_code,
                           protocol::user_spot_create_result_t
                             result,
                           const std::string &spot,
                           std::uint64_t generation,
                           std::optional<
                             protocol::application_payload_t>
                             application_reply = std::nullopt) {
                          protocol::user_spot_create_reply_t reply{
                            {request.correlation,
                             terminal_result,
                             failure_code},
                            result,
                            spot,
                            generation};
                          user_spot_terminal_record_t stored{
                            protocol::command::userSpotCreate,
                            request.deadline_unix_ms,
                            request_fingerprint,
                            protocol::
                              encode_user_spot_create_reply (
                                request.correlation,
                                terminal_result, failure_code,
                                result, spot, generation),
                            application_reply};
                          {
                              std::lock_guard lock (_mutex);
                              _user_spot_terminals
                                .insert_or_assign (
                                  operation_key,
                                  std::move (stored));
                          }
                          (void) _transport
                            ->reply_user_spot_create (
                              mailbox_record, reply,
                              std::move (
                                application_reply));
                      };
                    if (!store || !materializer) {
                        terminal (
                          105, static_cast<std::uint32_t> (
                                 protocol::framework_error_code::
                                   requestFailed),
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    if (request.deadline_unix_ms
                        <= unix_milliseconds_now ()) {
                        terminal (
                          101, 0,
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    const auto &global_id = request.spot_id;
                    const auto read =
                      store
                        ->read_authority (
                          {std::to_string (
                             static_cast<int> (
                               placement_object_kind_t::
                                 user_spot))
                           + ":" + global_id})
                        .result ()
                        .value ();
                    const auto *snapshot =
                      std::get_if<authority_snapshot_t> (&read);
                    const auto &reservation =
                      request.reservation;
                    const auto exact =
                      snapshot
                      && snapshot->store_version
                           == reservation
                                .expected_store_version
                      && snapshot->object_generation
                           == reservation.object_generation
                      && snapshot
                           ->authority_owner_generation
                           == reservation
                                .authority_owner_generation
                      && snapshot->owner.owner_id
                           == reservation.target_owner_id
                      && snapshot->owner.lease_generation
                           == reservation
                                .target_owner_lease_generation
                      && snapshot->allocation.object_kind
                           == placement_object_kind_t::
                                user_spot
                      && snapshot->allocation.stable_type
                           == request.stable_type
                      && snapshot->allocation.target.node_rid.value ()
                           == node_rid_t::from_string (
                                zlink::routing_id_t::from (
                                  reservation
                                    .target_node_routing_id)
                                  .to_string ())
                                .value ()
                      && snapshot->allocation.target
                           .node_lifecycle_generation
                           == reservation
                                .target_node_generation
                      && snapshot->allocation.capacity_bundle.spot_slots
                           == reservation
                                .pending_capacity_delta;
                    if (!exact) {
                        const auto stale =
                          snapshot
                          && snapshot->object_generation
                               != reservation
                                    .object_generation;
                        const auto type_mismatch =
                          snapshot
                          && snapshot->allocation.object_kind
                               == placement_object_kind_t::
                                    user_spot
                          && snapshot->allocation.stable_type
                               != request.stable_type;
                        terminal (
                          107,
                          static_cast<std::uint32_t> (
                            stale
                              ? protocol::framework_error_code::
                                  spotGenerationStale
                              : type_mismatch
                                ? protocol::
                                    framework_error_code::
                                      spotTypeMismatch
                                : protocol::
                                    framework_error_code::
                                      spotMoving),
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    const auto fence =
                      public_fence (
                        reservation,
                        snapshot->allocation.target.mesh_name,
                        request.stable_type);
                    const auto &pending =
                      snapshot->pending_creation;
                    if (!pending
                        || pending->reservation_id
                             != fence.reservation_id) {
                        terminal (
                          107,
                          static_cast<std::uint32_t> (
                            protocol::framework_error_code::
                              spotMoving),
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    const auto creation_payload =
                      runtime::decode_inline_creation_content (
                        pending->request_content_reference);
                    if (!creation_payload
                        || pending->request_encoded_size
                             != creation_payload->size ()
                        || pending->request_sha256
                             != runtime::sha256 (*creation_payload)) {
                        terminal (
                          105,
                          static_cast<std::uint32_t> (
                            protocol::framework_error_code::
                              requestFailed),
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    const stateful::object_ref_t exact_ref{
                      stateful::object_kind_t::user_spot,
                      request.spot_id,
                      reservation.object_generation,
                      reservation.authority_owner_generation,
                      snapshot->allocation.target.mesh_name,
                      std::string (
                        snapshot->allocation.target.node_rid.value ())};
                    if (snapshot->allocation.state
                        == placement_allocation_state_t::active) {
                        const auto existing = _objects.find (
                          stateful::object_kind_t::user_spot,
                          exact_ref.key);
                        if (!existing
                            || *existing != exact_ref) {
                            terminal (
                              105,
                              static_cast<std::uint32_t> (
                                protocol::
                                  framework_error_code::
                                    requestFailed),
                              protocol::
                                user_spot_create_result_t::
                                  rejected,
                              {}, 0);
                            continue;
                        }
                        terminal (
                          0, 0,
                          protocol::user_spot_create_result_t::
                            existing,
                          request.spot_id,
                          exact_ref.object_generation);
                        continue;
                    }
                    auto local =
                      _objects.begin_reserved_user_spot (
                        exact_ref, request.stable_type,
                        [&] {
                            std::vector<std::uint8_t> bytes;
                            bytes.reserve (
                              creation_payload->size ());
                            for (const auto value :
                                 *creation_payload)
                                bytes.push_back (
                                  std::to_integer<
                                    std::uint8_t> (value));
                            return bytes;
                        } ());
                    if (local.status
                        == stateful::create_status_t::existing) {
                        terminal (
                          0, 0,
                          protocol::user_spot_create_result_t::
                            existing,
                          request.spot_id,
                          exact_ref.object_generation);
                        continue;
                    }
                    if (!local.factory_owner) {
                        terminal (
                          local.error
                                == stateful::stateful_error_t::
                                     generation_stale
                            ? 107
                            : 108,
                          static_cast<std::uint32_t> (
                            local.error
                                  == stateful::
                                       stateful_error_t::moving
                              ? protocol::
                                  framework_error_code::
                                    spotMoving
                              : local.error
                                    == stateful::
                                         stateful_error_t::
                                           generation_stale
                                ? protocol::
                                    framework_error_code::
                                      spotGenerationStale
                                : protocol::
                                    framework_error_code::
                                      requestFailed),
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    user_spot_materialize_result_t
                      materialized;
                    try {
                        materialized =
                          materializer (
                            exact_ref, request.stable_type,
                            *creation_payload);
                    }
                    catch (...) {
                        (void) _objects.abort_create (
                          local.attempt);
                        (void) store
                          ->abort (
                            {{placement_object_kind_t::
                                user_spot,
                              global_id},
                             public_fence (
                               reservation,
                               snapshot->allocation.target.mesh_name,
                               request.stable_type)})
                          .result ();
                        terminal (
                          105,
                          static_cast<std::uint32_t> (
                            protocol::framework_error_code::
                              spotCreateFailed),
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    if (!materialized.accepted) {
                        (void) store
                          ->abort (
                            {{placement_object_kind_t::
                                user_spot,
                              global_id},
                             fence})
                          .result ()
                          .value ();
                        (void) _objects.abort_create (
                          local.attempt);
                        terminal (
                          0, 0,
                          protocol::user_spot_create_result_t::
                            rejected,
                          request.spot_id,
                          exact_ref.object_generation,
                          std::move (
                            materialized.application_reply));
                        continue;
                    }
                    const auto committed =
                      store
                        ->commit (
                          {{placement_object_kind_t::user_spot,
                           global_id},
                           fence,
                           ready_user_spot_authority_payload (
                             exact_ref,
                             request.stable_type)})
                        .result ()
                        .value ();
                    const auto *ready =
                      std::get_if<object_committed_t> (
                        &committed);
                    const auto *already =
                      std::get_if<
                        object_already_committed_t> (
                        &committed);
                    if (!ready && !already) {
                        (void) _objects.abort_create (
                          local.attempt);
                        terminal (
                          107,
                          static_cast<std::uint32_t> (
                            protocol::framework_error_code::
                              spotMoving),
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    auto local_commit =
                      _objects.commit_create (local.attempt);
                    if (local_commit
                        != stateful::stateful_error_t::none) {
                        std::vector<std::uint8_t> creation_bytes;
                        creation_bytes.reserve (
                          creation_payload->size ());
                        for (const auto value : *creation_payload)
                            creation_bytes.push_back (
                              std::to_integer<std::uint8_t> (value));
                        const auto reconciled =
                          _objects.begin_reserved_user_spot (
                            exact_ref, request.stable_type,
                            std::move (creation_bytes));
                        if (reconciled.status
                              == stateful::create_status_t::existing
                            && reconciled.object == exact_ref)
                            local_commit =
                              stateful::stateful_error_t::none;
                        else if (
                          (reconciled.status
                             == stateful::create_status_t::reserved
                           || reconciled.status
                                == stateful::create_status_t::joined)
                          && reconciled.attempt != 0)
                            local_commit = _objects.commit_create (
                              reconciled.attempt);
                    }
                    if (local_commit
                        != stateful::stateful_error_t::none) {
                        terminal (
                          105,
                          static_cast<std::uint32_t> (
                            protocol::framework_error_code::
                              spotCreateFailed),
                          protocol::user_spot_create_result_t::
                            rejected,
                          {}, 0);
                        continue;
                    }
                    {
                        std::lock_guard lock (_mutex);
                        _spots.insert_or_assign (
                          exact_ref.key, exact_ref);
                    }
                    terminal (
                      0, 0,
                      protocol::user_spot_create_result_t::
                        created,
                      request.spot_id,
                      exact_ref.object_generation,
                      std::move (
                        materialized.application_reply));
                    continue;
                }

                const auto request =
                  protocol::decode_user_spot_close_header (
                    mailbox_record.parts.front ());
                auto fingerprint_request = request;
                fingerprint_request.correlation = 1;
                const auto request_fingerprint =
                  protocol::encode_user_spot_close_header (
                    fingerprint_request);
                const auto operation_key =
                  user_spot_operation_key (
                    request.source_node_routing_id,
                    request.source_node_generation,
                    request.operation);
                std::optional<user_spot_terminal_record_t>
                  cached;
                {
                    std::lock_guard lock (_mutex);
                    const auto found =
                      _user_spot_terminals.find (
                        operation_key);
                    if (found != _user_spot_terminals.end ()) {
                        if (user_spot_operation_replay_expired (
                              found->second.deadline_unix_ms,
                              unix_milliseconds_now (),
                              _options
                                .user_spot_operation_replay_retention))
                            _user_spot_terminals.erase (found);
                        else
                            cached = found->second;
                    }
                }
                if (cached) {
                    if (cached->kind
                          != protocol::command::userSpotClose
                        || cached->request_fingerprint
                             != request_fingerprint)
                        throw protocol::service_wire_error_t (
                          "user spot operation identity was reused with a different request");
                    auto reply =
                      protocol::decode_user_spot_close_reply (
                        cached->header);
                    reply.header.correlation =
                      request.correlation;
                    (void) _transport->reply_user_spot_close (
                      mailbox_record, reply);
                    continue;
                }
                {
                    std::lock_guard lock (_mutex);
                    if (_user_spot_terminals.size ()
                        >= _options.user_spot_operation_capacity) {
                        const auto now = unix_milliseconds_now ();
                        std::erase_if (
                          _user_spot_terminals,
                          [this, now] (const auto &entry) {
                              return user_spot_operation_replay_expired (
                                entry.second.deadline_unix_ms, now,
                                _options
                                  .user_spot_operation_replay_retention);
                          });
                    }
                    if (_user_spot_terminals.size ()
                        >= _options.user_spot_operation_capacity) {
                        protocol::user_spot_close_reply_t reply{
                          {request.correlation, 103, 0}, false};
                        (void) _transport->reply_user_spot_close (
                          mailbox_record, reply);
                        continue;
                    }
                }
                auto terminal =
                  [&] (std::uint32_t terminal_result,
                       std::uint32_t failure_code,
                       bool closed) {
                      protocol::user_spot_close_reply_t reply{
                        {request.correlation,
                         terminal_result,
                         failure_code},
                        closed};
                      user_spot_terminal_record_t stored{
                        protocol::command::userSpotClose,
                        request.deadline_unix_ms,
                        request_fingerprint,
                        protocol::encode_user_spot_close_reply (
                          request.correlation,
                          terminal_result, failure_code,
                          closed),
                        std::nullopt};
                      {
                          std::lock_guard lock (_mutex);
                          _user_spot_terminals.insert_or_assign (
                            operation_key, std::move (stored));
                      }
                      (void) _transport
                        ->reply_user_spot_close (
                          mailbox_record, reply);
                  };
                if (!store) {
                    terminal (
                      105, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               requestFailed),
                      false);
                    continue;
                }
                if (request.deadline_unix_ms
                    <= unix_milliseconds_now ()) {
                    terminal (101, 0, false);
                    continue;
                }
                const auto &global_id = request.target.spot_id;
                const auto read =
                  store
                    ->read_authority (
                      {std::to_string (
                         static_cast<int> (
                           placement_object_kind_t::
                             user_spot))
                       + ":" + global_id})
                    .result ()
                    .value ();
                const auto *snapshot =
                  std::get_if<authority_snapshot_t> (&read);
                if (!snapshot) {
                    terminal (0, 0, false);
                    continue;
                }
                const auto &target = request.target;
                if (snapshot->object_generation
                      != target.object_generation) {
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotGenerationStale),
                      false);
                    continue;
                }
                if (snapshot->authority_owner_generation
                      != target.authority_owner_generation
                    || snapshot->store_version
                         != target.expected_store_version
                    || snapshot->allocation.object_kind
                         != placement_object_kind_t::user_spot
                    || snapshot->allocation.state
                         != placement_allocation_state_t::active
                    || snapshot->allocation.target.node_rid.value ()
                         != node_rid_t::from_string (
                              zlink::routing_id_t::from (
                                target
                                  .target_node_routing_id)
                                .to_string ())
                              .value ()
                    || snapshot->allocation.target
                         .node_lifecycle_generation
                         != target.target_node_generation) {
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotMoving),
                      false);
                    continue;
                }
                const stateful::object_ref_t exact_ref{
                  stateful::object_kind_t::user_spot,
                  target.spot_id,
                  target.object_generation,
                  target.authority_owner_generation,
                  snapshot->allocation.target.mesh_name,
                  std::string (
                    snapshot->allocation.target.node_rid.value ())};
                if (snapshot->payload
                    != ready_user_spot_authority_payload (
                      exact_ref, snapshot->allocation.stable_type)) {
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotMoving),
                      false);
                    continue;
                }
                const auto local = _objects.find (
                  stateful::object_kind_t::user_spot,
                  exact_ref.key);
                if (!local || *local != exact_ref) {
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotMoving),
                      false);
                    continue;
                }
                const authority_key_t authority_key{
                  std::to_string (
                    static_cast<int> (
                      placement_object_kind_t::user_spot))
                  + ":" + global_id};
                const auto sealed =
                  store
                    ->compare_exchange_authority (
                      authority_key,
                      snapshot->store_version,
                      authority_put_t{
                        closing_user_spot_authority_payload (
                          exact_ref),
                        authority_generation_transition_t::
                          preserve,
                        std::nullopt,
                        std::nullopt})
                    .result ()
                    .value ();
                const auto *closing =
                  std::get_if<authority_stored_t> (&sealed);
                if (!closing) {
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotMoving),
                      false);
                    continue;
                }
                const auto rollback_closing = [&] {
                    return std::holds_alternative<
                      authority_stored_t> (
                      store
                        ->compare_exchange_authority (
                          authority_key,
                          closing->snapshot.store_version,
                          authority_put_t{
                            snapshot->payload,
                            authority_generation_transition_t::
                              preserve,
                            std::nullopt,
                            std::nullopt})
                        .result ()
                        .value ());
                };
                const auto [close_error, close_token] =
                  _objects.begin_close_spot (exact_ref);
                if (close_error
                    == stateful::stateful_error_t::
                         generation_stale) {
                    (void) rollback_closing ();
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotGenerationStale),
                      false);
                    continue;
                }
                if (close_error
                    == stateful::stateful_error_t::moving) {
                    (void) rollback_closing ();
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotMoving),
                      false);
                    continue;
                }
                if (close_error
                      == stateful::stateful_error_t::not_found
                    || !close_token) {
                    (void) rollback_closing ();
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotMoving),
                      false);
                    continue;
                }
                if (_objects.commit_close_spot (
                      *close_token)
                    != stateful::stateful_error_t::none) {
                    (void) rollback_closing ();
                    terminal (
                      105, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               requestFailed),
                      false);
                    continue;
                }
                const auto deleted =
                  store
                    ->compare_exchange_authority (
                      authority_key,
                      closing->snapshot.store_version,
                      authority_delete_t{})
                    .result ()
                    .value ();
                if (!std::holds_alternative<
                      authority_deleted_t> (deleted)) {
                    terminal (
                      107, static_cast<std::uint32_t> (
                             protocol::framework_error_code::
                               spotMoving),
                      false);
                    continue;
                }
                {
                    std::lock_guard lock (_mutex);
                    _spots.erase (exact_ref.key);
                }
                terminal (0, 0, true);
            }
            catch (const protocol::service_wire_error_t &) {
                if (mailbox_record.request_sequence
                    && mailbox_record.correlation)
                    (void) _transport->reply_failure (
                      mailbox_record, 104,
                      static_cast<std::uint32_t> (
                        protocol::framework_error_code::
                          requestProtocolError));
            }
            catch (...) {
                if (mailbox_record.request_sequence
                    && mailbox_record.correlation)
                    (void) _transport->reply_failure (
                      mailbox_record, 105,
                      static_cast<std::uint32_t> (
                        protocol::framework_error_code::
                          requestFailed));
            }
        }
        (void) _transport->mailbox ().release (*claim);
    }
    return count;
}

std::size_t public_host_runtime_t::dispatch_ready (
  const std::function<void (const ready_record_t &,
                            const receive_record_t &,
                            std::vector<zlink::message_t>)> &dispatch)
{
    if (!dispatch) {
        throw std::invalid_argument (
          "framework public host dispatch callback is required");
    }
    const auto now = mesh::service_liveness_registry_t::clock_t::now ();
    (void) _transport->drain_monitor_events (now);
    std::size_t count = 0;
    for (; count < 64; ++count) {
        const auto pumped = _transport->pump_one (now);
        if (pumped == mesh::raw_mesh_pump_result_t::no_data) {
            break;
        }
    }
    (void) _transport->expire_requests (
      foundation::operation_registry_t::clock_t::now ());
    count += dispatch_user_spot_operations ();

    std::map<std::pair<std::uint64_t, std::uint64_t>,
             std::pair<receive_record_t, std::vector<zlink::message_t>>>
      completions;
    {
        std::lock_guard lock (_mutex);
        completions.swap (_completions);
    }
    for (auto &[_, completion] : completions) {
        ready_record_t owner;
        owner.owner_kind = owner_kind_t::node;
        owner.domain = ready_domain_t::infrastructure;
        dispatch (owner, completion.first, std::move (completion.second));
        ++count;
    }

    std::vector<local_application_dispatch_t> local_dispatches;
    {
        std::lock_guard lock (_mutex);
        local_dispatches.swap (_local_application_dispatches);
    }
    for (auto &pending : local_dispatches) {
        dispatch (pending.owner, pending.record,
                  std::move (pending.parts));
        ++count;
    }

    while (auto claim = _transport->mailbox ().try_claim (
             mesh::service_mailbox_domain_t::application, 64,
             16u * 1024u * 1024u)) {
        for (auto &mailbox_record : claim->records) {
            try {
                const auto wire =
                  protocol::decode_header (mailbox_record.parts.front ());
                const auto kind = record_kind (wire.kind);
                ready_record_t owner;
                owner.domain = ready_domain_t::application;
                receive_record_t record;
                record.kind = kind;
                record.domain = ready_domain_t::application;
                record.operation_kind = operation_kind (kind);
                record.source_node_rid =
                  zlink::routing_id_t::from (
                    mailbox_record.source_routing_id);
                if (mailbox_record.correlation) {
                    record.operation_id = {
                      status ().lifecycle_generation (),
                      *mailbox_record.correlation};
                }
                if (is_request (kind)) {
                    record.reply_token = {
                      weak_from_this (),
                      std::make_shared<mesh::service_mailbox_record_t> (
                        mailbox_record)};
                }
                if (kind == record_kind_t::channel_send
                    || kind == record_kind_t::channel_request) {
                    owner.owner_kind = owner_kind_t::channel;
                    owner.channel_name =
                      kind == record_kind_t::channel_send
                        ? protocol::decode_channel_send_header (
                            mailbox_record.parts.front ())
                        : protocol::decode_channel_request_header (
                            mailbox_record.parts.front ())
                            .channel_name;
                    record.channel_name = owner.channel_name;
                } else if (kind == record_kind_t::spot_send
                           || kind == record_kind_t::spot_request) {
                    owner.owner_kind = owner_kind_t::spot;
                    const auto spot = protocol::decode_spot_message_header (
                      mailbox_record.parts.front (), wire.kind);
                    owner.spot_id = spot.target.spot_id;
                } else if (kind == record_kind_t::actor_send
                           || kind == record_kind_t::actor_request) {
                    owner.owner_kind = owner_kind_t::actor;
                    const auto actor =
                      protocol::decode_actor_message_header (
                        mailbox_record.parts.front (), wire.kind);
                    std::string actor_type;
                    {
                        std::lock_guard lock (_mutex);
                        const auto found = _actors.find (
                          actor.target.actor_id);
                        if (found != _actors.end ()) {
                            actor_type = found->second.first;
                        }
                    }
                    owner.actor = actor_ref_t (
                      node_rid_t::from_string (
                        status ().routing_id ().to_string ()),
                      std::move (actor_type),
                      actor.target.actor_id,
                      actor.target.object_generation);
                } else {
                    owner.owner_kind = owner_kind_t::node;
                }
                const auto payload =
                  protocol::decode_application_payload (
                    mailbox_record.parts[1]);
                dispatch (
                  owner, record, decode_application (payload));
                ++count;
            }
            catch (const protocol::service_wire_error_t &) {
                if (mailbox_record.request_sequence
                    && mailbox_record.correlation) {
                    (void) _transport->reply_failure (
                      mailbox_record, 104,
                      static_cast<std::uint32_t> (
                        protocol::framework_error_code::
                          requestProtocolError));
                }
            }
        }
        (void) _transport->mailbox ().release (*claim);
    }
    return count;
}

bool public_host_runtime_t::prepare_actor_transfer (
  const actor_transfer_prepare_t &prepare,
  actor_transfer_token_t &token,
  actor_transfer_prepare_result_t &result)
{
    auto actor = resolve_actor (prepare.actor);
    auto target = resolve_spot (prepare.target_spot_id);
    if (!actor || !target) {
        return false;
    }
    auto [error, membership] =
      _objects.begin_membership_move (*actor, *target);
    if (error != stateful::stateful_error_t::none) {
        return false;
    }
    token._host = shared_from_this ();
    token._membership = membership;
    token._role = prepare.role;
    token._terminal = false;
    result.current_actor = framework_actor_ref (
      *actor, std::string (prepare.actor.actor_type ()));
    result.membership_epoch = actor->authority_owner_generation;
    return true;
}

bool public_host_runtime_t::reply (
  const reply_token_t &token,
  const std::vector<zlink::message_t> &parts)
{
    try {
        if (token.local_reply) {
            return token.local_reply (parts);
        }
        return token.request
               && _transport->reply (
                 *token.request, encode_application (parts));
    }
    catch (const zlink::submit_error_t &) {
        return false;
    }
}

std::optional<stateful::object_ref_t>
public_host_runtime_t::resolve_actor (const actor_ref_t &actor) const
{
    std::lock_guard lock (_mutex);
    const auto found = _actors.find (std::string (actor.actor_id ()));
    if (found == _actors.end ()
        || found->second.second.object_generation
             != actor.generation ()) {
        return std::nullopt;
    }
    return found->second.second;
}

std::optional<stateful::object_ref_t>
public_host_runtime_t::resolve_spot (
  const std::string &spot_id) const
{
    std::lock_guard lock (_mutex);
    const auto found = _spots.find (spot_id);
    return found == _spots.end ()
             ? std::optional<stateful::object_ref_t>{}
             : std::make_optional (found->second);
}

protocol::application_payload_t
public_host_runtime_t::encode_application (
  const std::vector<zlink::message_t> &parts,
  std::span<const std::uint8_t>) const
{
    return {
      parts.empty () ? std::string{} : "framework-envelope",
      std::string (multipart_content_type),
      encode_parts (parts)};
}

std::vector<zlink::message_t>
public_host_runtime_t::decode_application (
  const protocol::application_payload_t &payload) const
{
    if (payload.content_type != multipart_content_type) {
        throw protocol::service_wire_error_t (
          "framework application payload content type is unsupported");
    }
    return decode_parts (payload.payload);
}

actor_ref_t public_host_runtime_t::framework_actor_ref (
  const stateful::object_ref_t &object,
  std::string actor_type) const
{
    return actor_ref_t (
      node_rid_t::from_string (object.node_id),
      std::move (actor_type),
      object.key,
      object.object_generation);
}

operation_id_t public_host_runtime_t::next_operation ()
{
    std::lock_guard lock (_mutex);
    const auto low = _next_operation++;
    if (low == 0 || _next_operation == 0) {
        _next_operation = 1;
        throw std::overflow_error (
          "framework public host operation id is exhausted");
    }
    return {status ().lifecycle_generation (), low};
}

zlink::submit_result_t public_host_runtime_t::begin_local_actor_join (
  const actor_ref_t &actor,
  const std::string &target_spot_id,
  std::uint64_t target_spot_generation,
  const std::vector<zlink::message_t> &parts,
  operation_id_t &operation)
{
    operation = next_operation ();
    const auto current = resolve_actor (actor);
    const auto target = resolve_spot (target_spot_id);
    auto reject = [&] {
        receive_record_t completion;
        completion.kind = record_kind_t::completion;
        completion.domain = ready_domain_t::infrastructure;
        completion.operation_id = operation;
        completion.operation_kind = operation_kind_t::actor_join;
        completion.source_node_rid = status ().routing_id ();
        completion.join_completion = actor_join_completion_t{
          join_admission_t::rejected, actor};
        std::lock_guard lock (_mutex);
        _completions.emplace (
          std::make_pair (operation.high, operation.low),
          std::make_pair (std::move (completion),
                          std::vector<zlink::message_t>{}));
    };
    if (!current || !target
        || target->object_generation != target_spot_generation) {
        reject ();
        return zlink::submit_result_t::ok;
    }
    auto [error, membership] =
      _objects.begin_membership_move (*current, *target);
    if (error != stateful::stateful_error_t::none) {
        reject ();
        return zlink::submit_result_t::ok;
    }

    const auto actor_type = std::string (actor.actor_type ());
    std::weak_ptr<public_host_runtime_t> weak = shared_from_this ();
    ready_record_t owner{
      .owner_kind = owner_kind_t::spot,
      .domain = ready_domain_t::application,
      .spot_id = target_spot_id};
    receive_record_t record;
    record.kind = record_kind_t::spot_control;
    record.domain = ready_domain_t::application;
    record.operation_id = operation;
    record.operation_kind = operation_kind_t::actor_join;
    record.source_node_rid = status ().routing_id ();
    record.actor_control = actor_control_t{
      lifecycle_kind_t::joined, actor};
    record.reply_token.local_actor_join =
      [weak, operation, actor_type, membership] (
        actor_join_result_t result,
        const std::vector<zlink::message_t> &reply) {
          const auto host = weak.lock ();
          return host
                 && host->complete_local_actor_join (
                   operation, actor_type, membership, result, reply);
      };
    {
        std::lock_guard lock (_mutex);
        _local_application_dispatches.push_back (
          local_application_dispatch_t{
            std::move (owner), std::move (record), parts});
    }
    return zlink::submit_result_t::ok;
}

bool public_host_runtime_t::complete_local_actor_join (
  operation_id_t operation,
  std::string actor_type,
  stateful::membership_token_t membership,
  actor_join_result_t result,
  const std::vector<zlink::message_t> &parts)
{
    receive_record_t completion;
    completion.kind = record_kind_t::completion;
    completion.domain = ready_domain_t::infrastructure;
    completion.operation_id = operation;
    completion.operation_kind = operation_kind_t::actor_join;
    completion.source_node_rid = status ().routing_id ();

    if (result == actor_join_result_t::accepted) {
        auto [error, current] =
          _objects.commit_membership_move (membership);
        if (error != stateful::stateful_error_t::none) {
            completion.terminal_result = 1;
            completion.join_completion = actor_join_completion_t{
              join_admission_t::rejected,
              framework_actor_ref (membership.actor, actor_type)};
        } else {
            const auto actor =
              framework_actor_ref (current, actor_type);
            {
                std::lock_guard lock (_mutex);
                const auto found = _actors.find (current.key);
                if (found != _actors.end ())
                    found->second.second = current;
            }
            completion.join_completion = actor_join_completion_t{
              join_admission_t::accepted, actor};
        }
    } else {
        (void) _objects.abort_membership_move (membership);
        completion.join_completion = actor_join_completion_t{
          join_admission_t::rejected,
          framework_actor_ref (membership.actor, actor_type)};
    }

    std::lock_guard lock (_mutex);
    return _completions.emplace (
      std::make_pair (operation.high, operation.low),
      std::make_pair (std::move (completion), parts))
      .second;
}

bool public_host_runtime_t::enqueue_local_actor_message (
  const actor_ref_t &target,
  record_kind_t kind,
  const std::vector<zlink::message_t> &parts,
  std::optional<operation_id_t> operation)
{
    if (kind != record_kind_t::actor_send
        && kind != record_kind_t::actor_request) {
        return false;
    }
    const auto current = resolve_actor (target);
    if (!current) {
        return false;
    }

    ready_record_t owner{
      .owner_kind = owner_kind_t::actor,
      .domain = ready_domain_t::application,
      .actor = framework_actor_ref (
        *current, std::string (target.actor_type ()))};
    receive_record_t record;
    record.kind = kind;
    record.domain = ready_domain_t::application;
    record.source_node_rid = status ().routing_id ();
    if (operation) {
        record.operation_id = *operation;
        std::weak_ptr<public_host_runtime_t> weak =
          shared_from_this ();
        record.reply_token.host = weak;
        record.reply_token.local_reply =
          [weak, operation = *operation] (
            const std::vector<zlink::message_t> &reply) {
              const auto host = weak.lock ();
              return host
                     && host->complete_local_request (
                       operation, reply);
          };
    }
    std::lock_guard lock (_mutex);
    _local_application_dispatches.push_back (
      local_application_dispatch_t{
        std::move (owner), std::move (record), parts});
    return true;
}

bool public_host_runtime_t::complete_local_request (
  operation_id_t operation,
  const std::vector<zlink::message_t> &parts)
{
    receive_record_t completion;
    completion.kind = record_kind_t::completion;
    completion.domain = ready_domain_t::infrastructure;
    completion.operation_id = operation;
    completion.source_node_rid = status ().routing_id ();
    std::lock_guard lock (_mutex);
    return _completions.emplace (
      std::make_pair (operation.high, operation.low),
      std::make_pair (std::move (completion), parts))
      .second;
}

void public_host_runtime_t::complete_operation (
  operation_id_t operation,
  operation_kind_t kind,
  foundation::operation_terminal_t terminal,
  std::vector<std::uint8_t> payload)
{
    receive_record_t record;
    record.kind = record_kind_t::completion;
    record.domain = ready_domain_t::infrastructure;
    record.operation_id = operation;
    record.operation_kind = kind;
    record.source_node_rid = status ().routing_id ();
    switch (terminal) {
        case foundation::operation_terminal_t::completed:
            record.terminal_result = 0;
            break;
        case foundation::operation_terminal_t::timed_out:
            record.terminal_result = static_cast<int> (
              zlink::request_result_t::timed_out);
            break;
        case foundation::operation_terminal_t::shutdown:
            record.terminal_result = static_cast<int> (
              zlink::request_result_t::terminated);
            break;
        default:
            record.terminal_result = static_cast<int> (
              zlink::request_result_t::internal_error);
            break;
    }
    std::vector<zlink::message_t> parts;
    if (record.terminal_result == 0) {
        try {
            parts = decode_application (
              protocol::decode_application_payload (payload));
        }
        catch (const protocol::service_wire_error_t &) {
            record.terminal_result = static_cast<int> (
              zlink::request_result_t::protocol_error);
        }
    }
    std::lock_guard lock (_mutex);
    _completions.insert_or_assign (
      std::make_pair (operation.high, operation.low),
      std::make_pair (std::move (record), std::move (parts)));
}

bool actor_transfer_token_t::valid () const noexcept
{
    return !_terminal && _membership.value != 0 && !_host.expired ();
}

bool actor_transfer_token_t::commit (std::uint64_t)
{
    auto host = _host.lock ();
    if (!host || _terminal) {
        return false;
    }
    const auto [error, _] =
      host->objects ().commit_membership_move (_membership);
    _terminal = true;
    return error == stateful::stateful_error_t::none;
}

bool actor_transfer_token_t::activate ()
{
    return commit (_membership.actor.authority_owner_generation);
}

void actor_transfer_token_t::abort () noexcept
{
    if (auto host = _host.lock (); host && !_terminal) {
        (void) host->objects ().abort_membership_move (_membership);
    }
    _terminal = true;
}

zlink::submit_result_t reply (
  const reply_token_t &token,
  const std::vector<zlink::message_t> &parts)
{
    const auto host = token.host.lock ();
    return host && host->reply (token, parts)
             ? zlink::submit_result_t::ok
             : zlink::submit_result_t::terminated;
}

bool actor_join_reply (
  const reply_token_t &token,
  actor_join_result_t result,
  const std::vector<zlink::message_t> &parts)
{
    if (token.local_actor_join) {
        return token.local_actor_join (result, parts);
    }
    if (result != actor_join_result_t::accepted) {
        const auto host = token.host.lock ();
        if (!host || !token.request) {
            return false;
        }
        try {
            return host->transport ().reply_failure (
              *token.request, 106,
              static_cast<std::uint32_t> (
                protocol::framework_error_code::requestRejected));
        }
        catch (const zlink::submit_error_t &) {
            return false;
        }
    }
    return reply (token, parts) == zlink::submit_result_t::ok;
}

} // namespace zlink::framework::runtime::host
