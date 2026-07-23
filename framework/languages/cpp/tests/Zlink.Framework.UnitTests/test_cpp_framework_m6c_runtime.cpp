/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/stateful/maintenance_runtime.hpp"

#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace zlink::framework::runtime::stateful;

struct test_context_t
{
    int failures = 0;

    void require (bool condition, const char *message)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << "V11-M6C-CPP: " << message << '\n';
    }
};

std::string authority_key (object_kind_t kind, const std::string &key)
{
    return std::to_string (static_cast<int> (kind)) + ":" + key;
}

class memory_relocation_store_t final : public relocation_store_port_t
{
  public:
    relocation_stored_t put (
      const std::vector<std::uint8_t> &payload,
      std::chrono::hours retention) override
    {
        if (retention != std::chrono::hours (24))
            throw std::runtime_error ("unexpected retention");
        std::function<void ()> callback;
        {
            std::lock_guard lock (mutex);
            callback = on_put;
        }
        if (callback)
            callback ();
        std::lock_guard lock (mutex);
        const auto reference = "root-" + std::to_string (++next_reference);
        roots[reference] = payload;
        log.push_back ("put");
        return {reference, maintenance_runtime_t::crc32c (payload)};
    }

    std::optional<std::vector<std::uint8_t>>
    get (const std::string &reference) override
    {
        std::lock_guard lock (mutex);
        const auto found = roots.find (reference);
        return found == roots.end ()
                 ? std::optional<std::vector<std::uint8_t>>{}
                 : std::make_optional (found->second);
    }

    void remove (const std::string &reference) override
    {
        std::lock_guard lock (mutex);
        roots.erase (reference);
        removed.push_back (reference);
        log.push_back ("remove");
    }

    void erase_without_authority_change (const std::string &reference)
    {
        std::lock_guard lock (mutex);
        roots.erase (reference);
    }

    std::mutex mutex;
    std::map<std::string, std::vector<std::uint8_t>> roots;
    std::vector<std::string> removed;
    std::vector<std::string> log;
    std::function<void ()> on_put;
    std::uint64_t next_reference = 0;
};

class memory_authority_store_t final : public authority_relocation_port_t
{
  public:
    authority_publish_result_t publish (
      const object_ref_t &source,
      std::string target_node_id,
      std::string relocation_reference,
      std::uint32_t checksum_crc32c,
      inventory_digest_t inventory_digest) override
    {
        std::lock_guard lock (mutex);
        log.push_back ("publish");
        if (force_conflict)
            return {authority_publish_status_t::conflict, read_locked (
                      source.kind, source.key)};
        auto target = source;
        target.node_id = std::move (target_node_id);
        ++target.authority_owner_generation;
        authority_relocation_reference_t reference{
          .source = source,
          .target = target,
          .relocation_reference = std::move (relocation_reference),
          .checksum_crc32c = checksum_crc32c,
          .inventory_digest = inventory_digest};
        rows[authority_key (source.kind, source.key)] = reference;
        if (throw_after_publish)
            throw std::runtime_error ("response lost after authority commit");
        return {authority_publish_status_t::published, reference};
    }

    std::optional<authority_relocation_reference_t>
    read (object_kind_t kind, const std::string &key) override
    {
        std::lock_guard lock (mutex);
        return read_locked (kind, key);
    }

    std::optional<authority_relocation_reference_t>
    read_locked (object_kind_t kind, const std::string &key)
    {
        const auto found = rows.find (authority_key (kind, key));
        return found == rows.end ()
                 ? std::optional<authority_relocation_reference_t>{}
                 : std::make_optional (found->second);
    }

    std::mutex mutex;
    std::map<std::string, authority_relocation_reference_t> rows;
    std::vector<std::string> log;
    bool force_conflict = false;
    bool throw_after_publish = false;
};

object_ref_t create_actor (
  stateful_object_runtime_t &runtime,
  std::string key,
  std::string node = "node-a")
{
    runtime.replace_placement_candidates (
      {{.mesh_name = "mesh",
        .node_id = std::move (node),
        .stable_types = {"actor"},
        .weight = 100,
        .active_capacity = 100,
        .active_count = 0,
        .pending_capacity = 100,
        .pending_count = 0}});
    auto created = runtime.begin_create (
      {.kind = object_kind_t::actor,
       .key = std::move (key),
       .stable_type = "actor",
       .mesh_name = std::optional<std::string>{"mesh"},
       .creation_request = {},
       .exclusive = true,
       .instance_intent = false});
    if (created.status != create_status_t::reserved
        || runtime.commit_create (created.attempt) != stateful_error_t::none)
        throw std::runtime_error ("actor creation failed");
    return created.object;
}

inventory_digest_t digest_with (std::uint8_t value)
{
    inventory_digest_t digest{};
    digest.fill (value);
    return digest;
}

void test_envelope_round_trip (test_context_t &test)
{
    frozen_object_state_t frozen{
      .owner =
        {.kind = object_kind_t::actor,
         .key = "actor-a",
         .object_generation = 7,
         .authority_owner_generation = 9,
         .mesh_name = "mesh",
         .node_id = "node-a"},
      .stable_type = "actor",
      .pending_application = {{1, {1, 2}}, {2, {3}}},
      .timers = {{11, 100, 50, 3}}};
    const auto digest = digest_with (0x5a);
    const auto encoded = maintenance_runtime_t::encode (frozen, digest);
    const auto decoded = maintenance_runtime_t::decode (encoded);
    test.require (decoded.has_value (), "relocation envelope must decode");
    test.require (decoded && decoded->first == frozen,
                  "queue and timer state must round-trip");
    test.require (decoded && decoded->second == digest,
                  "inventory digest must round-trip");
    test.require (maintenance_runtime_t::crc32c (encoded) != 0,
                  "CRC32C must be computed for the immutable root");
}

void test_publication_and_handoff (test_context_t &test)
{
    stateful_object_runtime_t source;
    const auto actor = create_actor (source, "actor-a");
    test.require (
      source.enqueue (
        actor, turn_domain_t::application, {1, {1}})
        == stateful_error_t::none,
      "source queue setup must succeed");
    test.require (
      source.register_timer (actor, {7, 100, 0, 2})
        == stateful_error_t::none,
      "source timer setup must succeed");

    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    authority->throw_after_publish = true;
    roots->on_put = [&] {
        test.require (
          source.enqueue (
            actor, turn_domain_t::application, {2, {2}})
            == stateful_error_t::none,
          "post-seal ingress must enter the bounded hold");
        test.require (
          source.enqueue (
            actor, turn_domain_t::infrastructure, {9, {9}})
            == stateful_error_t::none,
          "infrastructure work must remain admissible while sealed");
    };
    int terminal_observations = 0;
    maintenance_runtime_t runtime (
      source, authority, roots, {},
      [&] (const relocation_result_t &) { ++terminal_observations; });
    const auto result = runtime.relocate (
      actor, "node-b", 1024 * 1024, digest_with (1));
    test.require (result.terminal == relocation_terminal_t::completed,
                  "durable relocation must complete");
    test.require (terminal_observations == 1,
                  "terminal observation must be emitted exactly once");
    test.require (
      roots->log.size () == 1 && roots->log.front () == "put"
        && authority->log.size () == 1
        && authority->log.front () == "publish",
      "immutable root must be stored before authority publication");
    test.require (result.authority
                    && source.pending (
                         result.authority->target,
                         turn_domain_t::application)
                         == 2,
                  "frozen queue must precede held ingress after commit");
    test.require (result.authority
                    && source.pending (
                         result.authority->target,
                         turn_domain_t::infrastructure)
                         == 1,
                  "infrastructure queue must remain available through commit");
    if (result.authority) {
        const auto [first_error, first] =
          source.try_claim (
            result.authority->target, turn_domain_t::application);
        test.require (first_error == stateful_error_t::none
                        && first && first->sequence == 1,
                      "frozen queue order must be preserved");
        (void) source.complete_claim (
          result.authority->target, turn_domain_t::application);
        const auto [second_error, second] =
          source.try_claim (
            result.authority->target, turn_domain_t::application);
        test.require (second_error == stateful_error_t::none
                        && second && second->sequence == 2,
                      "held ingress must follow the frozen queue");
        test.require (source.timers (result.authority->target).size () == 1,
                      "logical timer registration must survive commit");
    }
    test.require (runtime.gate_snapshot () == relocation_gate_snapshot_t{},
                  "all scheduler permits must be released at terminal");
}

void test_conflict_aborts_without_losing_ingress (test_context_t &test)
{
    stateful_object_runtime_t source;
    const auto actor = create_actor (source, "actor-conflict");
    (void) source.enqueue (
      actor, turn_domain_t::application, {1, {1}});
    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    authority->force_conflict = true;
    roots->on_put = [&] {
        (void) source.enqueue (
          actor, turn_domain_t::application, {2, {2}});
    };
    maintenance_runtime_t runtime (source, authority, roots);
    const auto result = runtime.relocate (
      actor, "node-b", 1024 * 1024, digest_with (2));
    test.require (result.terminal == relocation_terminal_t::conflict,
                  "authority CAS conflict must be closed");
    test.require (roots->removed.size () == 1,
                  "CAS loser root must be removed as an orphan");
    test.require (
      source.pending (actor, turn_domain_t::application) == 2,
      "precommit abort must restore frozen then held ingress");
}

void test_recovery_and_data_loss (test_context_t &test)
{
    stateful_object_runtime_t source;
    const auto actor = create_actor (source, "actor-recovery");
    (void) source.enqueue (
      actor, turn_domain_t::application, {4, {9}});
    (void) source.register_timer (actor, {3, 50, 10, 5});
    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    maintenance_runtime_t coordinator (source, authority, roots);
    const auto moved = coordinator.relocate (
      actor, "node-b", 1024 * 1024, digest_with (3));
    test.require (moved.authority.has_value (),
                  "published relocation must expose recovery authority");

    stateful_object_runtime_t recovered;
    const auto recovery = coordinator.recover (
      object_kind_t::actor, "actor-recovery", recovered);
    test.require (recovery.terminal == relocation_terminal_t::completed,
                  "published root must restore into an empty target runtime");
    test.require (
      moved.authority
        && recovered.pending (
             moved.authority->target, turn_domain_t::application)
             == 1
        && recovered.timers (moved.authority->target).size () == 1,
      "recovery must restore queue and logical timer state");

    if (moved.authority)
        roots->erase_without_authority_change (
          moved.authority->relocation_reference);
    stateful_object_runtime_t missing_target;
    const auto missing = coordinator.recover (
      object_kind_t::actor, "actor-recovery", missing_target);
    test.require (
      missing.terminal == relocation_terminal_t::data_lost
        && missing.reason == relocation_reason_t::payload_missing,
      "published missing payload must be terminal data loss");
    test.require (
      authority->read (object_kind_t::actor, "actor-recovery").has_value (),
      "data loss must not roll authority back to the source");
}

void test_permit_precedes_seal (test_context_t &test)
{
    stateful_object_runtime_t source;
    const auto first = create_actor (source, "actor-first");
    const auto second = create_actor (source, "actor-second");
    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    std::mutex gate;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    roots->on_put = [&] {
        std::unique_lock lock (gate);
        entered = true;
        changed.notify_all ();
        changed.wait (lock, [&] { return release; });
    };
    relocation_limits_t limits;
    limits.outbound_units = 1;
    maintenance_runtime_t runtime (
      source, authority, roots, limits);
    relocation_result_t first_result;
    std::thread worker ([&] {
        first_result = runtime.relocate (
          first, "node-b", 1024, digest_with (4));
    });
    {
        std::unique_lock lock (gate);
        changed.wait (lock, [&] { return entered; });
    }
    const auto second_result = runtime.relocate (
      second, "node-b", 1024, digest_with (5));
    test.require (
      second_result.terminal == relocation_terminal_t::blocked
        && second_result.reason
             == relocation_reason_t::permit_unavailable,
      "unit without permits must remain unsealed");
    test.require (
      source.enqueue (
        second, turn_domain_t::application, {1, {1}})
        == stateful_error_t::none,
      "permit failure must leave normal admission open");
    {
        std::lock_guard lock (gate);
        release = true;
    }
    changed.notify_all ();
    worker.join ();
    test.require (first_result.terminal == relocation_terminal_t::completed,
                  "permitted unit must complete after store resumes");
}

} // namespace

int main ()
{
    test_context_t test;
    test_envelope_round_trip (test);
    test_publication_and_handoff (test);
    test_conflict_aborts_without_losing_ingress (test);
    test_recovery_and_data_loss (test);
    test_permit_precedes_seal (test);
    return test.failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
