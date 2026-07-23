/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/stateful/stateful_object_runtime.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace zlink::framework::runtime::stateful
{

using inventory_digest_t = std::array<std::uint8_t, 32>;

struct relocation_stored_t
{
    std::string reference;
    std::uint32_t checksum_crc32c = 0;
};

class relocation_store_port_t
{
  public:
    virtual ~relocation_store_port_t () = default;
    virtual relocation_stored_t put (
      const std::vector<std::uint8_t> &payload,
      std::chrono::hours retention) = 0;
    virtual std::optional<std::vector<std::uint8_t>>
    get (const std::string &reference) = 0;
    virtual void remove (const std::string &reference) = 0;
};

struct authority_relocation_reference_t
{
    object_ref_t source;
    object_ref_t target;
    std::string relocation_reference;
    std::uint32_t checksum_crc32c = 0;
    inventory_digest_t inventory_digest{};
};

enum class authority_publish_status_t
{
    published,
    conflict,
    failed
};

struct authority_publish_result_t
{
    authority_publish_status_t status = authority_publish_status_t::failed;
    std::optional<authority_relocation_reference_t> current;
};

class authority_relocation_port_t
{
  public:
    virtual ~authority_relocation_port_t () = default;
    virtual authority_publish_result_t publish (
      const object_ref_t &source,
      std::string target_node_id,
      std::string relocation_reference,
      std::uint32_t checksum_crc32c,
      inventory_digest_t inventory_digest) = 0;
    virtual std::optional<authority_relocation_reference_t>
    read (object_kind_t kind, const std::string &key) = 0;
};

struct relocation_limits_t
{
    std::size_t outbound_units = 64;
    std::size_t inbound_units = 64;
    std::size_t capture_callbacks = 8;
    std::size_t restore_callbacks = 8;
    std::size_t payload_bytes = 256u * 1024u * 1024u;
};

struct relocation_gate_snapshot_t
{
    std::size_t outbound_units = 0;
    std::size_t inbound_units = 0;
    std::size_t capture_callbacks = 0;
    std::size_t restore_callbacks = 0;
    std::size_t payload_bytes = 0;

    friend bool operator== (const relocation_gate_snapshot_t &,
                            const relocation_gate_snapshot_t &) = default;
};

enum class relocation_terminal_t
{
    completed,
    blocked,
    conflict,
    store_failed,
    data_lost,
    recovery_required
};

enum class relocation_reason_t
{
    none,
    permit_unavailable,
    turn_active,
    payload_bound_exceeded,
    store_write_failed,
    checksum_mismatch,
    authority_conflict,
    authority_publish_failed,
    payload_missing,
    inventory_mismatch,
    restore_failed
};

struct relocation_result_t
{
    relocation_terminal_t terminal = relocation_terminal_t::blocked;
    relocation_reason_t reason = relocation_reason_t::none;
    std::optional<authority_relocation_reference_t> authority;
};

class maintenance_runtime_t
{
  public:
    using observer_t = std::function<void (const relocation_result_t &)>;

    maintenance_runtime_t (
      stateful_object_runtime_t &objects,
      std::shared_ptr<authority_relocation_port_t> authority,
      std::shared_ptr<relocation_store_port_t> relocations,
      relocation_limits_t limits = {},
      observer_t observer = {});

    relocation_result_t relocate (
      const object_ref_t &source,
      std::string target_node_id,
      std::size_t encoded_upper_bound,
      inventory_digest_t inventory_digest);
    relocation_result_t recover (
      object_kind_t kind,
      const std::string &key,
      stateful_object_runtime_t &target);

    relocation_gate_snapshot_t gate_snapshot () const;

    static std::uint32_t crc32c (
      const std::vector<std::uint8_t> &payload) noexcept;
    static std::vector<std::uint8_t> encode (
      const frozen_object_state_t &frozen,
      const inventory_digest_t &inventory_digest);
    static std::optional<std::pair<frozen_object_state_t, inventory_digest_t>>
    decode (const std::vector<std::uint8_t> &payload);

  private:
    class permit_t
    {
      public:
        permit_t () = default;
        permit_t (maintenance_runtime_t *owner, std::size_t payload);
        ~permit_t ();
        permit_t (permit_t &&other) noexcept;
        permit_t &operator= (permit_t &&other) noexcept;
        permit_t (const permit_t &) = delete;
        permit_t &operator= (const permit_t &) = delete;

        explicit operator bool () const noexcept;

      private:
        maintenance_runtime_t *_owner = nullptr;
        std::size_t _payload = 0;
    };

    permit_t try_acquire (std::size_t payload);
    void release (std::size_t payload) noexcept;
    relocation_result_t finish (relocation_result_t result);

    stateful_object_runtime_t &_objects;
    std::shared_ptr<authority_relocation_port_t> _authority;
    std::shared_ptr<relocation_store_port_t> _relocations;
    relocation_limits_t _limits;
    observer_t _observer;
    mutable std::mutex _gate_mutex;
    relocation_gate_snapshot_t _gate;
};

} // namespace zlink::framework::runtime::stateful
