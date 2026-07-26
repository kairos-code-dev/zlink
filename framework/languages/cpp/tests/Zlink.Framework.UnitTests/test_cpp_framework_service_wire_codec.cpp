/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/protocol/service_wire_codec.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

namespace protocol = zlink::framework::runtime::protocol;
namespace mesh = zlink::framework::runtime::mesh;

namespace
{
std::vector<std::uint8_t> from_hex (std::string_view value)
{
    assert (value.size () % 2 == 0);
    const auto digit = [] (char value) -> std::uint8_t {
        if (value >= '0' && value <= '9')
            return static_cast<std::uint8_t> (value - '0');
        if (value >= 'a' && value <= 'f')
            return static_cast<std::uint8_t> (value - 'a' + 10);
        assert (false);
        return 0;
    };
    std::vector<std::uint8_t> result;
    result.reserve (value.size () / 2);
    for (std::size_t index = 0; index < value.size (); index += 2) {
        result.push_back (static_cast<std::uint8_t> (
          (digit (value[index]) << 4) | digit (value[index + 1])));
    }
    return result;
}
}

int main ()
{
    const auto node_send = protocol::encode_node_send_header ();
    const auto decoded_node_send = protocol::decode_header (node_send);
    assert (decoded_node_send.kind == protocol::command::nodeSend);
    assert (decoded_node_send.flags == 0);
    const auto channel_send =
      protocol::encode_channel_send_header ("alpha");
    assert (protocol::decode_channel_send_header (channel_send) == "alpha");
    const protocol::application_payload_t application{
      "Probe", "application/json", {1, 2, 3}};
    const auto application_wire =
      protocol::encode_application_payload (application);
    assert (protocol::decode_application_payload (application_wire)
            == application);
    auto admission_descriptor = mesh::service_node_descriptor_t{
      "codec-mesh", std::vector<std::uint8_t>{'n', 'o', 'd', 'e'},
      1, 7, "tcp://127.0.0.1:7000",
      {{"alpha", 100}}, mesh::service_node_state_t::serving};
    admission_descriptor.security_identity = "test";
    admission_descriptor.application_version = 42;
    for (const auto kind :
         {protocol::command::hello, protocol::command::admit,
          protocol::command::update}) {
        const auto encoded =
          protocol::encode_route_mesh_admission (kind, admission_descriptor);
        assert (protocol::decode_route_mesh_admission (
                  encoded, kind, admission_descriptor.node_routing_id)
                == admission_descriptor);
    }
    assert (protocol::decode_reject (protocol::encode_reject (7)) == 7);
    const protocol::client_server_client_admission_t client_admission{
      "alpha", "security-a", 1024 * 1024};
    assert (protocol::decode_client_server_client_admission (
              protocol::encode_client_server_client_admission (
                protocol::command::hello, client_admission),
              protocol::command::hello)
            == client_admission);
    const protocol::client_server_server_admission_t server_admission{
      "alpha",
      {'s', 'e', 'r', 'v', 'e', 'r'},
      3,
      7,
      100,
      mesh::service_node_state_t::serving,
      "security-a",
      1024 * 1024,
      "tcp://127.0.0.1:7002"};
    assert (protocol::decode_client_server_server_admission (
              protocol::encode_client_server_server_admission (
                protocol::command::admit, server_admission),
              protocol::command::admit)
            == server_admission);
    constexpr std::uint64_t correlation = 0x0102030405060708ULL;
    assert (protocol::decode_node_request_header (
              protocol::encode_node_request_header (correlation))
            == correlation);
    const auto channel_request = protocol::decode_channel_request_header (
      protocol::encode_channel_request_header (correlation, "alpha"));
    assert (channel_request.correlation == correlation);
    assert (channel_request.channel_name == "alpha");
    const auto reply = protocol::decode_reply_header (
      protocol::encode_reply_header (correlation, 0, 0));
    assert (reply.correlation == correlation);
    assert (reply.terminal_result == 0);
    assert (reply.failure_code == 0);
    const protocol::spot_route_fence_t spot_fence{
      {'s', 'p', 'o', 't'},
      3,
      {'n', 'o', 'd', 'e'},
      5,
      7};
    const auto spot_request = protocol::decode_spot_message_header (
      protocol::encode_spot_message_header (
        protocol::command::spotRequest, {'s', 'o', 'u', 'r', 'c', 'e'},
        spot_fence, correlation),
      protocol::command::spotRequest);
    assert (spot_request.correlation == correlation);
    assert (spot_request.target == spot_fence);
    const protocol::actor_route_fence_t actor_fence{
      "actor-1", 11, {'n', 'o', 'd', 'e'}, 5, 9};
    const std::optional<std::pair<std::string, std::uint64_t>>
      source_actor{std::pair{"actor-0", 4}};
    const auto actor_send = protocol::decode_actor_message_header (
      protocol::encode_actor_message_header (
        protocol::command::actorSend, source_actor, actor_fence),
      protocol::command::actorSend);
    assert (!actor_send.correlation);
    assert (actor_send.source_actor == source_actor);
    assert (actor_send.target == actor_fence);
    const protocol::user_spot_create_header_t user_spot_create{
      correlation,
      {4, 5},
      {'s', 'o', 'u', 'r', 'c', 'e'},
      7,
      {'s', 'p', 'o', 't'},
      "room",
      {"reservation-1",
       "store-1",
       9,
       11,
       {'t', 'a', 'r', 'g', 'e', 't'},
       13,
       "owner-1",
       15,
       1},
      1700000000000ULL};
    assert (protocol::decode_user_spot_create_header (
              protocol::encode_user_spot_create_header (
                user_spot_create))
            == user_spot_create);
    const protocol::instance_spot_activation_header_t instance_activation{
      {{'t', 'a', 'r', 'g', 'e', 't'},
       7,
       "spot-1",
       "main",
       "quest",
       "descriptor-9",
       1700000000000ULL},
      3,
      {'s', 'o', 'u', 'r', 'c', 'e'},
      std::string ("entry"),
      true,
      {0, 9},
      11,
      true};
    const auto encoded_instance_activation =
      protocol::encode_instance_spot_activation_header (
        instance_activation);
    assert (encoded_instance_activation[3]
            == static_cast<std::uint8_t> (
              protocol::command::instanceSpot));
    assert (protocol::decode_instance_spot_activation_header (
              encoded_instance_activation)
            == instance_activation);
    auto trailing_instance_activation = encoded_instance_activation;
    trailing_instance_activation.push_back (0);
    bool rejected_instance_activation = false;
    try {
        (void) protocol::decode_instance_spot_activation_header (
          trailing_instance_activation);
    }
    catch (const protocol::service_wire_error_t &) {
        rejected_instance_activation = true;
    }
    assert (rejected_instance_activation);
    const protocol::instance_activation_recovery_t instance_recovery{
      instance_activation,
      from_hex ("01010574726163650003616263"),
      {"quest.start", "application/json",
       {'{', '"', 'x', '"', ':', '1', '}'}}};
    const auto golden_instance_recovery = from_hex (
      "5a4c4941010000000000a00673706f742d31057175657374046d61696e067461"
      "7267657400000000000000070c64657363726970746f722d3906736f75726365"
      "00000000000000030105656e7472790200000000000000000000000000000009"
      "000000000000000b0000018bcfe5680001010105747261636500036162630100"
      "0000280b71756573742e7374617274106170706c69636174696f6e2f6a736f6e"
      "000000077b2278223a317de138c97b");
    assert (protocol::encode_instance_activation_recovery (
              instance_recovery)
            == golden_instance_recovery);
    assert (protocol::decode_instance_activation_recovery (
              golden_instance_recovery)
            == instance_recovery);
    auto corrupt_instance_recovery = golden_instance_recovery;
    corrupt_instance_recovery.back () ^= 1;
    bool rejected_instance_recovery = false;
    try {
        (void) protocol::decode_instance_activation_recovery (
          corrupt_instance_recovery);
    }
    catch (const protocol::service_wire_error_t &) {
        rejected_instance_recovery = true;
    }
    assert (rejected_instance_recovery);
    const protocol::user_spot_close_header_t user_spot_close{
      correlation,
      {6, 7},
      {'s', 'o', 'u', 'r', 'c', 'e'},
      7,
      {{'s', 'p', 'o', 't'},
       9,
       {'t', 'a', 'r', 'g', 'e', 't'},
       13,
       11,
       "store-2"},
      1700000001000ULL};
    assert (protocol::decode_user_spot_close_header (
              protocol::encode_user_spot_close_header (
                user_spot_close))
            == user_spot_close);
    const auto create_reply =
      protocol::decode_user_spot_create_reply (
        protocol::encode_user_spot_create_reply (
          correlation, 0, 0,
          protocol::user_spot_create_result_t::created,
          {'s', 'p', 'o', 't'}, 9));
    assert (create_reply.header.correlation == correlation);
    assert (create_reply.result
            == protocol::user_spot_create_result_t::created);
    assert (create_reply.object_generation == 9);
    const auto close_reply =
      protocol::decode_user_spot_close_reply (
        protocol::encode_user_spot_close_reply (
          correlation, 0, 0, true));
    assert (close_reply.closed);
    const auto stale_create_reply =
      protocol::decode_user_spot_create_reply (
        protocol::encode_user_spot_create_reply (
          correlation, 107,
          static_cast<std::uint32_t> (
            protocol::framework_error_code::spotGenerationStale),
          protocol::user_spot_create_result_t::rejected, {}, 0));
    assert (stale_create_reply.header.failure_code == 33);
    const auto type_mismatch_create_reply =
      protocol::decode_user_spot_create_reply (
        protocol::encode_user_spot_create_reply (
          correlation, 107,
          static_cast<std::uint32_t> (
            protocol::framework_error_code::spotTypeMismatch),
          protocol::user_spot_create_result_t::rejected, {}, 0));
    assert (
      type_mismatch_create_reply.header.failure_code
      == static_cast<std::uint32_t> (
        protocol::framework_error_code::spotTypeMismatch));
    const auto moving_close_reply =
      protocol::decode_user_spot_close_reply (
        protocol::encode_user_spot_close_reply (
          correlation, 107,
          static_cast<std::uint32_t> (
            protocol::framework_error_code::spotMoving),
          false));
    assert (
      moving_close_reply.header.failure_code
      == static_cast<std::uint32_t> (
        protocol::framework_error_code::spotMoving));
    const auto deadline_create_reply =
      protocol::decode_user_spot_create_reply (
        protocol::encode_user_spot_create_reply (
          correlation, 101, 0,
          protocol::user_spot_create_result_t::rejected, {}, 0));
    assert (deadline_create_reply.header.terminal_result == 101);
    const auto busy_create_reply =
      protocol::decode_user_spot_create_reply (
        protocol::encode_user_spot_create_reply (
          correlation, 108, 0,
          protocol::user_spot_create_result_t::rejected, {}, 0));
    assert (busy_create_reply.header.terminal_result == 108);
    auto trailing_user_spot_create =
      protocol::encode_user_spot_create_header (
        user_spot_create);
    trailing_user_spot_create.push_back (0);
    bool rejected_user_spot_create = false;
    try {
        (void) protocol::decode_user_spot_create_header (
          trailing_user_spot_create);
    }
    catch (const protocol::service_wire_error_t &) {
        rejected_user_spot_create = true;
    }
    assert (rejected_user_spot_create);
    for (auto malformed_payload : std::vector<std::vector<std::uint8_t>>{
           [&] { auto value = application_wire; value[0] = 2; return value; } (),
           [&] { auto value = application_wire; value[4] += 1; return value; } (),
           [&] { auto value = application_wire; value.push_back (0); return value; } (),
           [&] {
               auto value = application_wire;
               value[6] = 0xc0;
               return value;
           } ()}) {
        bool rejected = false;
        try {
            static_cast<void> (
              protocol::decode_application_payload (malformed_payload));
        } catch (const protocol::service_wire_error_t &) {
            rejected = true;
        }
        assert (rejected);
    }

    const protocol::session_relocation_seal_t session_seal{
      {1, 2},
      {"coord", 3, {0xc1}, 4, "v5"},
      protocol::relocation_role_t::source,
      {"actor", 6, {0xe1}, 14, 10, 15},
      {0xa1},
      7,
      "owner",
      8,
      {0xb1},
      9};
    const auto encoded_session_seal =
      protocol::encode_session_relocation_seal (session_seal);
    assert (protocol::decode_session_relocation_seal (
              encoded_session_seal)
            == session_seal);
    const protocol::session_relocation_sealed_t session_sealed{
      session_seal.relocation,
      session_seal.coordinator,
      session_seal.actor,
      session_seal.session_owner_node_routing_id,
      session_seal.session_owner_node_generation,
      session_seal.session_owner_id,
      session_seal.session_owner_lease_generation,
      session_seal.session_routing_id,
      session_seal.binding_generation,
      13};
    assert (protocol::decode_session_relocation_sealed (
              protocol::encode_session_relocation_sealed (
                session_sealed))
            == session_sealed);

    const protocol::session_relocation_route_t session_route{
      {1, 2},
      {"coord", 3, {0xc1}, 4, "v5"},
      protocol::relocation_role_t::target,
      {"actor", 6},
      {0xa1},
      7,
      "owner",
      8,
      {0xb1},
      9,
      {protocol::session_relocation_route_action_t::commit,
       10, 11, {0xd1}, 12, 13, 0}};
    const auto encoded_session_route =
      protocol::encode_session_relocation_route (session_route);
    assert (encoded_session_route == from_hex (
      "5a4d012c00"
      "00000000000000010000000000000002"
      "05636f6f7264000000000000000301c1"
      "00000000000000040002763502"
      "056163746f72000000000000000601a1"
      "0000000000000007056f776e6572"
      "000000000000000801b10000000000000009"
      "010022000000000000000a000000000000000b01d1"
      "000000000000000c000000000000000d"));
    assert (protocol::decode_session_relocation_route (
              encoded_session_route)
            == session_route);
    auto abort_session_route = session_route;
    abort_session_route.sender_role =
      protocol::relocation_role_t::source;
    abort_session_route.route = {
      protocol::session_relocation_route_action_t::abort,
      0, 0, {}, 0, 0, 10};
    assert (protocol::decode_session_relocation_route (
              protocol::encode_session_relocation_route (
                abort_session_route))
            == abort_session_route);

    const protocol::session_relocation_routed_t session_routed{
      {1, 2},
      {"coord", 3, {0xc1}, 4, "v5"},
      {"actor", 6},
      {0xa1},
      7,
      "owner",
      8,
      {0xb1},
      9,
      protocol::session_relocation_route_action_t::commit,
      11,
      13};
    const auto encoded_session_routed =
      protocol::encode_session_relocation_routed (session_routed);
    assert (protocol::decode_session_relocation_routed (
              encoded_session_routed)
            == session_routed);
    {
        auto malformed = encoded_session_route;
        malformed.push_back (0);
        bool rejected = false;
        try {
            static_cast<void> (
              protocol::decode_session_relocation_route (malformed));
        }
        catch (const protocol::service_wire_error_t &) {
            rejected = true;
        }
        assert (rejected);
    }

    constexpr std::uint64_t probe_id = 0x0102030405060708ULL;
    const auto probe = protocol::encode_liveness (protocol::command::livenessProbe, probe_id);
    const auto decoded_probe = protocol::decode_liveness (probe);
    assert (decoded_probe.kind == protocol::command::livenessProbe);
    assert (decoded_probe.probe_id == probe_id);

    const auto ack = protocol::encode_liveness (protocol::command::livenessAck,
                                                decoded_probe.probe_id);
    const auto decoded_ack = protocol::decode_liveness (ack);
    assert (decoded_ack.kind == protocol::command::livenessAck);
    assert (decoded_ack.probe_id == probe_id);

    for (auto malformed : std::vector<std::vector<std::uint8_t>>{
           std::vector<std::uint8_t> (probe.begin (), probe.end () - 1),
           [&] { auto value = probe; value.push_back (0); return value; } (),
           [&] { auto value = probe; value[0] = 0; return value; } (),
           [&] { auto value = probe; value[4] = 1; return value; } (),
           protocol::encode_liveness (protocol::command::livenessProbe, 1)}) {
        if (malformed.back () == 1 && malformed.size () == probe.size ()) {
            for (std::size_t index = 5; index < malformed.size (); ++index) malformed[index] = 0;
        }
        bool rejected = false;
        try {
            static_cast<void> (protocol::decode_liveness (malformed));
        } catch (const protocol::service_wire_error_t &) {
            rejected = true;
        }
        assert (rejected);
    }

    for (auto malformed_header : std::vector<std::vector<std::uint8_t>>{
           std::vector<std::uint8_t> (node_send.begin (), node_send.end () - 1),
           [&] { auto value = node_send; value[0] = 0; return value; } (),
           [&] { auto value = node_send; value[2] = 2; return value; } ()}) {
        bool rejected = false;
        try {
            static_cast<void> (protocol::decode_header (malformed_header));
        } catch (const protocol::service_wire_error_t &) {
            rejected = true;
        }
        assert (rejected);
    }
    for (const auto &malformed_request :
         {protocol::encode_node_request_header (correlation),
          protocol::encode_reply_header (correlation, 0, 0)}) {
        auto trailing = malformed_request;
        trailing.push_back (0);
        bool rejected = false;
        try {
            if (malformed_request.size () == 13) {
                static_cast<void> (
                  protocol::decode_node_request_header (trailing));
            } else {
                static_cast<void> (protocol::decode_reply_header (trailing));
            }
        } catch (const protocol::service_wire_error_t &) {
            rejected = true;
        }
        assert (rejected);
    }
    return 0;
}
