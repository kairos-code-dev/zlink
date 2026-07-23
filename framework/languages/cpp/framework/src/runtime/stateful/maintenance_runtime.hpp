/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/stateful/stateful_object_runtime.hpp"
#include "runtime/stateful/stream_session_registry.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <map>
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

struct aggregate_relocation_fence_t
{
    std::uint64_t value = 0;
};

enum class aggregate_publish_status_t
{
    prepared,
    committed,
    conflict,
    failed
};

struct aggregate_publish_result_t
{
    aggregate_publish_status_t status = aggregate_publish_status_t::failed;
    aggregate_relocation_fence_t fence;
    std::vector<authority_relocation_reference_t> current;
};

class aggregate_authority_port_t
{
  public:
    virtual ~aggregate_authority_port_t () = default;
    virtual aggregate_publish_result_t prepare (
      const std::vector<object_ref_t> &sources,
      std::string target_node_id,
      std::string relocation_reference,
      std::uint32_t checksum_crc32c,
      inventory_digest_t inventory_digest) = 0;
    virtual aggregate_publish_result_t commit (
      aggregate_relocation_fence_t fence) = 0;
    virtual void abort (aggregate_relocation_fence_t fence) = 0;
};

struct relocation_unit_t
{
    std::vector<object_ref_t> participants;

    friend bool operator== (const relocation_unit_t &,
                            const relocation_unit_t &) = default;
};

struct eligible_relocation_unit_t
{
    relocation_unit_t unit;
    std::string target_node_id;
    std::size_t encoded_upper_bound = 0;
    inventory_digest_t inventory_digest{};
};

enum class target_preflight_status_t
{
    eligible,
    target_unavailable,
    store_unavailable,
    relocation_disabled,
    state_incompatible
};

struct target_preflight_result_t
{
    target_preflight_status_t status =
      target_preflight_status_t::target_unavailable;
    std::vector<eligible_relocation_unit_t> units;
};

class target_preflight_port_t
{
  public:
    virtual ~target_preflight_port_t () = default;
    virtual target_preflight_result_t preflight (
      const std::vector<relocation_unit_t> &units) = 0;
};

struct maintenance_provider_set_t
{
    std::shared_ptr<authority_relocation_port_t> authority;
    std::shared_ptr<aggregate_authority_port_t> aggregate_authority;
    std::shared_ptr<relocation_store_port_t> relocations;
    std::shared_ptr<target_preflight_port_t> targets;
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

struct aggregate_relocation_result_t
{
    relocation_terminal_t terminal = relocation_terminal_t::blocked;
    relocation_reason_t reason = relocation_reason_t::none;
    std::vector<authority_relocation_reference_t> authority;
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
    maintenance_runtime_t (
      stateful_object_runtime_t &objects,
      maintenance_provider_set_t providers,
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
    aggregate_relocation_result_t relocate_aggregate (
      const std::vector<object_ref_t> &sources,
      std::string target_node_id,
      std::size_t encoded_upper_bound,
      inventory_digest_t inventory_digest);

    relocation_gate_snapshot_t gate_snapshot () const;

    static std::uint32_t crc32c (
      const std::vector<std::uint8_t> &payload) noexcept;
    static std::vector<std::uint8_t> encode (
      const frozen_object_state_t &frozen,
      const inventory_digest_t &inventory_digest);
    static std::optional<std::pair<frozen_object_state_t, inventory_digest_t>>
    decode (const std::vector<std::uint8_t> &payload);
    static std::vector<std::uint8_t> encode_aggregate (
      const std::vector<frozen_object_state_t> &participants,
      const inventory_digest_t &inventory_digest);

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
    std::shared_ptr<aggregate_authority_port_t> _aggregate_authority;
    std::shared_ptr<relocation_store_port_t> _relocations;
    relocation_limits_t _limits;
    observer_t _observer;
    mutable std::mutex _gate_mutex;
    relocation_gate_snapshot_t _gate;
};

enum class host_runtime_state_t
{
    preparing,
    serving,
    retiring,
    draining,
    stopped,
    error
};

enum class termination_intent_t
{
    retire,
    shutdown
};

enum class termination_outcome_t
{
    stopped,
    blocked,
    force_stopped
};

enum class termination_reason_t
{
    none,
    target_unavailable,
    store_unavailable,
    relocation_disabled,
    state_incompatible,
    deadline_exceeded,
    relocation_failed,
    teardown_failed,
    runtime_not_ready
};

struct termination_result_t
{
    termination_intent_t effective_intent =
      termination_intent_t::shutdown;
    termination_outcome_t outcome = termination_outcome_t::blocked;
    termination_reason_t reason = termination_reason_t::runtime_not_ready;

    friend bool operator== (const termination_result_t &,
                            const termination_result_t &) = default;
};

class host_maintenance_runtime_t
{
  public:
    using observer_t = std::function<void (const termination_result_t &)>;

    host_maintenance_runtime_t (
      stateful_object_runtime_t &objects,
      stream_session_registry_t &sessions,
      maintenance_runtime_t &relocation,
      std::shared_ptr<target_preflight_port_t> targets,
      observer_t observer = {});

    void mark_serving ();
    void mark_error ();
    host_runtime_state_t state () const;
    termination_result_t terminate (termination_intent_t intent);
    std::optional<termination_result_t> terminal_result () const;
    std::optional<termination_intent_t> intent_snapshot () const;

  private:
    static std::vector<relocation_unit_t> inventory_units (
      std::vector<object_inventory_t> inventory);
    termination_result_t run_retire ();
    termination_result_t run_shutdown (
      termination_intent_t effective_intent);
    void complete_attempt (
      std::uint64_t attempt, const termination_result_t &result);

    stateful_object_runtime_t &_objects;
    stream_session_registry_t &_sessions;
    maintenance_runtime_t &_relocation;
    std::shared_ptr<target_preflight_port_t> _targets;
    observer_t _observer;
    mutable std::mutex _mutex;
    std::condition_variable _changed;
    host_runtime_state_t _state = host_runtime_state_t::preparing;
    bool _active = false;
    bool _inventory_sealed = false;
    bool _shutdown_claimed = false;
    std::optional<termination_intent_t> _effective_intent;
    std::uint64_t _active_attempt = 0;
    std::uint64_t _next_attempt = 1;
    std::map<std::uint64_t, termination_result_t> _attempt_results;
    std::optional<termination_result_t> _terminal;
};

} // namespace zlink::framework::runtime::stateful
