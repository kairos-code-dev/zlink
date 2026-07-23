/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/stateful/maintenance_runtime.hpp"
#include "runtime/stateful/public_host_runtime.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::stateful
{
namespace
{

constexpr std::array<std::uint8_t, 4> envelope_magic{'Z', 'L', 'R', '1'};
constexpr std::chrono::hours relocation_retention{24};

void append_u32 (std::vector<std::uint8_t> &output, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        output.push_back (static_cast<std::uint8_t> (value >> shift));
}

void append_u64 (std::vector<std::uint8_t> &output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back (static_cast<std::uint8_t> (value >> shift));
}

bool append_bytes (std::vector<std::uint8_t> &output,
                   const std::vector<std::uint8_t> &value)
{
    if (value.size () > std::numeric_limits<std::uint32_t>::max ())
        return false;
    append_u32 (output, static_cast<std::uint32_t> (value.size ()));
    output.insert (output.end (), value.begin (), value.end ());
    return true;
}

bool append_text (std::vector<std::uint8_t> &output,
                  const std::string &value)
{
    return append_bytes (
      output, std::vector<std::uint8_t> (value.begin (), value.end ()));
}

class reader_t
{
  public:
    explicit reader_t (const std::vector<std::uint8_t> &input) :
        _input (input)
    {
    }

    std::optional<std::uint8_t> u8 ()
    {
        if (_offset == _input.size ())
            return std::nullopt;
        return _input[_offset++];
    }

    std::optional<std::uint32_t> u32 ()
    {
        if (_input.size () - _offset < 4)
            return std::nullopt;
        std::uint32_t value = 0;
        for (int count = 0; count != 4; ++count)
            value = (value << 8) | _input[_offset++];
        return value;
    }

    std::optional<std::uint64_t> u64 ()
    {
        if (_input.size () - _offset < 8)
            return std::nullopt;
        std::uint64_t value = 0;
        for (int count = 0; count != 8; ++count)
            value = (value << 8) | _input[_offset++];
        return value;
    }

    std::optional<std::vector<std::uint8_t>> bytes ()
    {
        const auto size = u32 ();
        if (!size || *size > _input.size () - _offset)
            return std::nullopt;
        std::vector<std::uint8_t> result (
          _input.begin () + static_cast<std::ptrdiff_t> (_offset),
          _input.begin () + static_cast<std::ptrdiff_t> (_offset + *size));
        _offset += *size;
        return result;
    }

    std::optional<std::string> text ()
    {
        const auto value = bytes ();
        return value
                 ? std::make_optional (
                     std::string (value->begin (), value->end ()))
                 : std::nullopt;
    }

    bool done () const noexcept { return _offset == _input.size (); }

  private:
    const std::vector<std::uint8_t> &_input;
    std::size_t _offset = 0;
};

} // namespace

maintenance_runtime_t::maintenance_runtime_t (
  stateful_object_runtime_t &objects,
  std::shared_ptr<authority_relocation_port_t> authority,
  std::shared_ptr<relocation_store_port_t> relocations,
  relocation_limits_t limits,
  observer_t observer) :
    _objects (objects),
    _authority (std::move (authority)),
    _relocations (std::move (relocations)),
    _limits (limits),
    _observer (std::move (observer))
{
    if (!_authority || !_relocations
        || _limits.outbound_units == 0
        || _limits.inbound_units == 0
        || _limits.capture_callbacks == 0
        || _limits.restore_callbacks == 0
        || _limits.payload_bytes == 0) {
        throw std::invalid_argument ("maintenance runtime configuration is invalid");
    }
}

relocation_result_t maintenance_runtime_t::relocate (
  const object_ref_t &source,
  std::string target_node_id,
  std::size_t encoded_upper_bound,
  inventory_digest_t inventory_digest)
{
    auto permit = try_acquire (encoded_upper_bound);
    if (!permit) {
        return finish (
          {relocation_terminal_t::blocked,
           relocation_reason_t::permit_unavailable,
           std::nullopt});
    }

    auto [seal_error, seal] = _objects.try_seal_relocation (source);
    if (seal_error != stateful_error_t::none) {
        return finish (
          {relocation_terminal_t::blocked,
           seal_error == stateful_error_t::backpressured
             ? relocation_reason_t::turn_active
             : relocation_reason_t::restore_failed,
           std::nullopt});
    }

    auto payload = encode (seal.frozen, inventory_digest);
    if (payload.empty () || payload.size () > encoded_upper_bound) {
        (void) _objects.abort_relocation (seal.token);
        return finish (
          {relocation_terminal_t::blocked,
           relocation_reason_t::payload_bound_exceeded,
           std::nullopt});
    }
    const auto checksum = crc32c (payload);

    relocation_stored_t stored;
    try {
        stored = _relocations->put (payload, relocation_retention);
    }
    catch (...) {
        (void) _objects.abort_relocation (seal.token);
        return finish (
          {relocation_terminal_t::store_failed,
           relocation_reason_t::store_write_failed,
           std::nullopt});
    }
    if (stored.reference.empty () || stored.checksum_crc32c != checksum) {
        if (!stored.reference.empty ()) {
            try {
                _relocations->remove (stored.reference);
            }
            catch (...) {
            }
        }
        (void) _objects.abort_relocation (seal.token);
        return finish (
          {relocation_terminal_t::store_failed,
           relocation_reason_t::checksum_mismatch,
           std::nullopt});
    }

    authority_publish_result_t published;
    bool publish_uncertain = false;
    try {
        published = _authority->publish (
          source, std::move (target_node_id), stored.reference,
          checksum, inventory_digest);
    }
    catch (...) {
        published.status = authority_publish_status_t::failed;
        publish_uncertain = true;
    }
    if (published.status != authority_publish_status_t::published
        || !published.current) {
        try {
            const auto current =
              _authority->read (source.kind, source.key);
            if (current
                && current->source == source
                && current->relocation_reference == stored.reference
                && current->checksum_crc32c == checksum
                && current->inventory_digest == inventory_digest) {
                published.status = authority_publish_status_t::published;
                published.current = current;
                publish_uncertain = false;
            } else {
                publish_uncertain = false;
            }
        }
        catch (...) {
            publish_uncertain = true;
        }
    }
    if (published.status != authority_publish_status_t::published
        || !published.current) {
        if (publish_uncertain) {
            return finish (
              {relocation_terminal_t::recovery_required,
               relocation_reason_t::authority_publish_failed,
               std::nullopt});
        }
        try {
            _relocations->remove (stored.reference);
        }
        catch (...) {
        }
        (void) _objects.abort_relocation (seal.token);
        return finish (
          {published.status == authority_publish_status_t::conflict
             ? relocation_terminal_t::conflict
             : relocation_terminal_t::store_failed,
           published.status == authority_publish_status_t::conflict
             ? relocation_reason_t::authority_conflict
             : relocation_reason_t::authority_publish_failed,
           published.current});
    }

    const auto [commit_error, committed] =
      _objects.commit_relocation (
        seal.token, published.current->target.node_id);
    if (commit_error != stateful_error_t::none
        || committed != published.current->target) {
        return finish (
          {relocation_terminal_t::recovery_required,
           relocation_reason_t::restore_failed,
           published.current});
    }
    return finish (
      {relocation_terminal_t::completed,
       relocation_reason_t::none,
       published.current});
}

relocation_result_t maintenance_runtime_t::recover (
  object_kind_t kind,
  const std::string &key,
  stateful_object_runtime_t &target)
{
    std::optional<authority_relocation_reference_t> authority;
    try {
        authority = _authority->read (kind, key);
    }
    catch (...) {
        return finish (
          {relocation_terminal_t::recovery_required,
           relocation_reason_t::authority_publish_failed,
           std::nullopt});
    }
    if (!authority) {
        return finish (
          {relocation_terminal_t::conflict,
           relocation_reason_t::authority_conflict,
           std::nullopt});
    }
    std::optional<std::vector<std::uint8_t>> payload;
    try {
        payload = _relocations->get (authority->relocation_reference);
    }
    catch (...) {
        return finish (
          {relocation_terminal_t::recovery_required,
           relocation_reason_t::store_write_failed,
           authority});
    }
    if (!payload) {
        return finish (
          {relocation_terminal_t::data_lost,
           relocation_reason_t::payload_missing,
           authority});
    }
    if (crc32c (*payload) != authority->checksum_crc32c) {
        return finish (
          {relocation_terminal_t::data_lost,
           relocation_reason_t::checksum_mismatch,
           authority});
    }
    auto decoded = decode (*payload);
    if (!decoded
        || decoded->second != authority->inventory_digest) {
        return finish (
          {relocation_terminal_t::data_lost,
           relocation_reason_t::inventory_mismatch,
           authority});
    }
    const auto restored =
      target.restore_relocation (
        std::move (decoded->first), authority->target);
    if (restored != stateful_error_t::none
        && !(restored == stateful_error_t::already_exists
             && target.find (kind, key) == authority->target)) {
        return finish (
          {relocation_terminal_t::recovery_required,
           relocation_reason_t::restore_failed,
           authority});
    }
    return finish (
      {relocation_terminal_t::completed,
       relocation_reason_t::none,
       authority});
}

relocation_gate_snapshot_t maintenance_runtime_t::gate_snapshot () const
{
    std::lock_guard lock (_gate_mutex);
    return _gate;
}

std::uint32_t maintenance_runtime_t::crc32c (
  const std::vector<std::uint8_t> &payload) noexcept
{
    std::uint32_t crc = 0xffffffffu;
    for (const auto byte : payload) {
        crc ^= byte;
        for (int bit = 0; bit != 8; ++bit)
            crc = (crc >> 1)
                  ^ (0x82f63b78u
                     & static_cast<std::uint32_t> (
                       -static_cast<std::int32_t> (crc & 1u)));
    }
    return ~crc;
}

std::vector<std::uint8_t> maintenance_runtime_t::encode (
  const frozen_object_state_t &frozen,
  const inventory_digest_t &inventory_digest)
{
    if (frozen.owner.key.empty ()
        || frozen.owner.mesh_name.empty ()
        || frozen.owner.node_id.empty ()
        || frozen.stable_type.empty ()
        || frozen.pending_application.size ()
             > std::numeric_limits<std::uint32_t>::max ()
        || frozen.timers.size ()
             > std::numeric_limits<std::uint32_t>::max ()) {
        return {};
    }
    std::vector<std::uint8_t> output (
      envelope_magic.begin (), envelope_magic.end ());
    output.push_back (static_cast<std::uint8_t> (frozen.owner.kind));
    if (!append_text (output, frozen.owner.key)
        || !append_text (output, frozen.stable_type)
        || !append_text (output, frozen.owner.mesh_name)
        || !append_text (output, frozen.owner.node_id)) {
        return {};
    }
    append_u64 (output, frozen.owner.object_generation);
    append_u64 (output, frozen.owner.authority_owner_generation);
    append_u32 (
      output,
      static_cast<std::uint32_t> (frozen.pending_application.size ()));
    for (const auto &record : frozen.pending_application) {
        append_u64 (output, record.sequence);
        if (!append_bytes (output, record.payload))
            return {};
    }
    append_u32 (
      output, static_cast<std::uint32_t> (frozen.timers.size ()));
    for (const auto &timer : frozen.timers) {
        append_u64 (output, timer.timer_id);
        append_u64 (output, timer.due_after_milliseconds);
        append_u64 (output, timer.period_milliseconds);
        append_u64 (output, timer.next_tick_sequence);
    }
    output.insert (
      output.end (), inventory_digest.begin (), inventory_digest.end ());
    return output;
}

std::optional<std::pair<frozen_object_state_t, inventory_digest_t>>
maintenance_runtime_t::decode (
  const std::vector<std::uint8_t> &payload)
{
    if (payload.size () < envelope_magic.size ()
                           + 1 + inventory_digest_t{}.size ()
        || !std::equal (
          envelope_magic.begin (), envelope_magic.end (), payload.begin ())) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> encoded (
      payload.begin () + static_cast<std::ptrdiff_t> (envelope_magic.size ()),
      payload.end ());
    reader_t reader (encoded);
    const auto kind = reader.u8 ();
    const auto key = reader.text ();
    const auto stable_type = reader.text ();
    const auto mesh_name = reader.text ();
    const auto node_id = reader.text ();
    const auto object_generation = reader.u64 ();
    const auto owner_generation = reader.u64 ();
    const auto pending_count = reader.u32 ();
    if (!kind || *kind > static_cast<std::uint8_t> (object_kind_t::instance_spot)
        || !key || key->empty () || !stable_type || stable_type->empty ()
        || !mesh_name || mesh_name->empty () || !node_id || node_id->empty ()
        || !object_generation || *object_generation == 0
        || !owner_generation || *owner_generation == 0 || !pending_count) {
        return std::nullopt;
    }

    frozen_object_state_t frozen{
      .owner =
        object_ref_t{
          .kind = static_cast<object_kind_t> (*kind),
          .key = *key,
          .object_generation = *object_generation,
          .authority_owner_generation = *owner_generation,
          .mesh_name = *mesh_name,
          .node_id = *node_id},
      .stable_type = *stable_type,
      .pending_application = {},
      .timers = {}};
    frozen.pending_application.reserve (*pending_count);
    for (std::uint32_t index = 0; index != *pending_count; ++index) {
        const auto sequence = reader.u64 ();
        auto bytes = reader.bytes ();
        if (!sequence || *sequence == 0 || !bytes)
            return std::nullopt;
        frozen.pending_application.push_back (
          turn_record_t{*sequence, std::move (*bytes)});
    }
    const auto timer_count = reader.u32 ();
    if (!timer_count)
        return std::nullopt;
    frozen.timers.reserve (*timer_count);
    for (std::uint32_t index = 0; index != *timer_count; ++index) {
        const auto timer_id = reader.u64 ();
        const auto due = reader.u64 ();
        const auto period = reader.u64 ();
        const auto next = reader.u64 ();
        if (!timer_id || *timer_id == 0 || !due || *due == 0
            || !period || !next || *next == 0) {
            return std::nullopt;
        }
        frozen.timers.push_back (
          logical_timer_t{*timer_id, *due, *period, *next});
    }
    inventory_digest_t digest{};
    for (auto &byte : digest) {
        const auto value = reader.u8 ();
        if (!value)
            return std::nullopt;
        byte = *value;
    }
    if (!reader.done ())
        return std::nullopt;
    return std::make_pair (std::move (frozen), digest);
}

maintenance_runtime_t::permit_t::permit_t (
  maintenance_runtime_t *owner,
  std::size_t payload) :
    _owner (owner), _payload (payload)
{
}

maintenance_runtime_t::permit_t::~permit_t ()
{
    if (_owner)
        _owner->release (_payload);
}

maintenance_runtime_t::permit_t::permit_t (
  permit_t &&other) noexcept :
    _owner (std::exchange (other._owner, nullptr)),
    _payload (std::exchange (other._payload, 0))
{
}

maintenance_runtime_t::permit_t &
maintenance_runtime_t::permit_t::operator= (
  permit_t &&other) noexcept
{
    if (this == &other)
        return *this;
    if (_owner)
        _owner->release (_payload);
    _owner = std::exchange (other._owner, nullptr);
    _payload = std::exchange (other._payload, 0);
    return *this;
}

maintenance_runtime_t::permit_t::operator bool () const noexcept
{
    return _owner != nullptr;
}

maintenance_runtime_t::permit_t
maintenance_runtime_t::try_acquire (std::size_t payload)
{
    if (payload == 0)
        return {};
    std::lock_guard lock (_gate_mutex);
    const bool empty = _gate.outbound_units == 0
                       && _gate.inbound_units == 0
                       && _gate.capture_callbacks == 0
                       && _gate.restore_callbacks == 0
                       && _gate.payload_bytes == 0;
    const bool oversized = payload > _limits.payload_bytes;
    if (_gate.outbound_units >= _limits.outbound_units
        || _gate.inbound_units >= _limits.inbound_units
        || _gate.capture_callbacks >= _limits.capture_callbacks
        || _gate.restore_callbacks >= _limits.restore_callbacks
        || (oversized ? !empty
                      : payload > _limits.payload_bytes - _gate.payload_bytes)) {
        return {};
    }
    ++_gate.outbound_units;
    ++_gate.inbound_units;
    ++_gate.capture_callbacks;
    ++_gate.restore_callbacks;
    _gate.payload_bytes += payload;
    return permit_t (this, payload);
}

void maintenance_runtime_t::release (std::size_t payload) noexcept
{
    std::lock_guard lock (_gate_mutex);
    --_gate.outbound_units;
    --_gate.inbound_units;
    --_gate.capture_callbacks;
    --_gate.restore_callbacks;
    _gate.payload_bytes -= payload;
}

relocation_result_t maintenance_runtime_t::finish (
  relocation_result_t result)
{
    if (_observer) {
        try {
            _observer (result);
        }
        catch (...) {
        }
    }
    return result;
}

} // namespace zlink::framework::runtime::stateful

namespace zlink::framework::runtime::host
{

void public_host_runtime_t::configure_maintenance (
  std::shared_ptr<stateful::authority_relocation_port_t> authority,
  std::shared_ptr<stateful::relocation_store_port_t> relocations,
  stateful::relocation_limits_t limits,
  stateful::maintenance_runtime_t::observer_t observer)
{
    std::lock_guard lock (_mutex);
    if (_started || _maintenance) {
        throw std::logic_error (
          "maintenance providers must be configured once before host start");
    }
    _maintenance = std::make_unique<stateful::maintenance_runtime_t> (
      _objects, std::move (authority), std::move (relocations),
      limits, std::move (observer));
}

stateful::maintenance_runtime_t *
public_host_runtime_t::maintenance () noexcept
{
    std::lock_guard lock (_mutex);
    return _maintenance.get ();
}

} // namespace zlink::framework::runtime::host
