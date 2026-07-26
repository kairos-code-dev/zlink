/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/stateful/maintenance_runtime.hpp"
#include "runtime/stateful/public_store_adapters.hpp"
#include "runtime/stateful/raw_stateful_dispatch.hpp"
#include "runtime/mesh/raw_mesh_node_owner.hpp"

#include <atomic>
#include <chrono>
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

inventory_digest_t digest_with (std::uint8_t value);

class public_memory_authority_store_t final :
    public zlink::framework::authority_store_t
{
  public:
    zlink::framework::task_t<
      zlink::framework::authority_read_result_t>
    read_authority (
      zlink::framework::authority_key_t,
      std::stop_token) override
    {
        if (!snapshot)
            return completed (
              zlink::framework::authority_read_result_t{
                zlink::framework::authority_missing_t{
                  std::chrono::system_clock::now ()}});
        return completed (
          zlink::framework::authority_read_result_t{*snapshot});
    }

    zlink::framework::task_t<
      zlink::framework::authority_compare_exchange_result_t>
    compare_exchange_authority (
      zlink::framework::authority_key_t,
      std::string expected_store_version,
      zlink::framework::authority_mutation_t mutation,
      std::stop_token) override
    {
        const auto *put =
          std::get_if<zlink::framework::authority_put_t> (
            &mutation);
        if (!snapshot || !put
            || expected_store_version != snapshot->store_version)
            return completed (
              zlink::framework::
                authority_compare_exchange_result_t{
                  zlink::framework::authority_conflict_t{
                    snapshot
                      ? zlink::framework::authority_read_result_t{
                          *snapshot}
                      : zlink::framework::authority_read_result_t{
                          zlink::framework::authority_missing_t{
                            std::chrono::system_clock::now ()}}}});
        if (put->generation_transition
            == zlink::framework::
                 authority_generation_transition_t::new_owner) {
            if (!put->target_owner
                || !put->relocation_capacity_fence)
                return completed (
                  zlink::framework::
                    authority_compare_exchange_result_t{
                      zlink::framework::authority_conflict_t{
                        zlink::framework::
                          authority_read_result_t{
                            *snapshot}}});
            observed_target_owner = put->target_owner;
            observed_capacity_fence =
              put->relocation_capacity_fence;
            ++snapshot->authority_owner_generation;
            snapshot->owner = *put->target_owner;
        }
        snapshot->store_version =
          std::to_string (
            std::stoull (snapshot->store_version) + 1);
        snapshot->payload = put->payload;
        snapshot->store_now = std::chrono::system_clock::now ();
        return completed (
          zlink::framework::
            authority_compare_exchange_result_t{
              zlink::framework::authority_stored_t{*snapshot}});
    }

    zlink::framework::task_t<
      zlink::framework::authority_scan_result_t>
    list_authorities (
      std::string,
      std::optional<zlink::framework::authority_scan_cursor_t>,
      std::size_t,
      std::stop_token) override
    {
        return completed (
          zlink::framework::authority_scan_result_t{
            zlink::framework::authority_page_t{}});
    }

    std::optional<zlink::framework::authority_snapshot_t> snapshot;
    std::optional<zlink::framework::location_owner_token_t>
      observed_target_owner;
    std::optional<zlink::framework::relocation_capacity_fence_t>
      observed_capacity_fence;

  private:
    template <typename T>
    static zlink::framework::task_t<T> completed (T value)
    {
        return zlink::framework::task_t<T> (
          zlink::framework::result_t<T>::success (
            std::move (value)));
    }
};

class public_memory_relocation_store_t final :
    public zlink::framework::relocation_store_t
{
  public:
    zlink::framework::task_t<zlink::framework::relocation_stored_t>
    put_relocation (
      std::vector<std::byte> payload,
      std::chrono::hours retention,
      std::stop_token) override
    {
        if (retention != std::chrono::hours (24))
            throw std::runtime_error ("unexpected retention");
        std::vector<std::uint8_t> checksum_input;
        checksum_input.reserve (payload.size ());
        for (const auto value : payload)
            checksum_input.push_back (
              std::to_integer<std::uint8_t> (value));
        const auto reference = "public-root";
        roots[reference] = std::move (payload);
        return completed (
          zlink::framework::relocation_stored_t{
            reference,
            maintenance_runtime_t::crc32c (checksum_input),
            std::chrono::system_clock::now () + retention,
            std::chrono::system_clock::now ()});
    }

    zlink::framework::task_t<
      zlink::framework::relocation_read_result_t>
    get_relocation (std::string reference, std::stop_token) override
    {
        const auto found = roots.find (reference);
        if (found == roots.end ())
            return completed (
              zlink::framework::relocation_read_result_t{
                zlink::framework::relocation_missing_t{}});
        return completed (
          zlink::framework::relocation_read_result_t{
            zlink::framework::relocation_found_t{found->second}});
    }

    zlink::framework::task_t<
      zlink::framework::relocation_renew_result_t>
    renew_relocation (
      std::string reference,
      std::chrono::hours retention,
      std::stop_token) override
    {
        if (!roots.contains (reference))
            return completed (
              zlink::framework::relocation_renew_result_t{
                zlink::framework::relocation_renew_missing_t{}});
        const auto now = std::chrono::system_clock::now ();
        return completed (
          zlink::framework::relocation_renew_result_t{
            zlink::framework::relocation_renewed_t{
              now + retention, now}});
    }

    zlink::framework::task_t<
      zlink::framework::relocation_delete_result_t>
    delete_relocation (std::string reference, std::stop_token) override
    {
        return completed (
          roots.erase (reference) > 0
            ? zlink::framework::relocation_delete_result_t::deleted
            : zlink::framework::relocation_delete_result_t::missing);
    }

  private:
    template <typename T>
    static zlink::framework::task_t<T> completed (T value)
    {
        return zlink::framework::task_t<T> (
          zlink::framework::result_t<T>::success (std::move (value)));
    }

    std::map<std::string, std::vector<std::byte>> roots;
};

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
      zlink::framework::location_owner_token_t target_owner,
      zlink::framework::relocation_capacity_fence_t,
      std::string relocation_reference,
      std::uint32_t checksum_crc32c,
      inventory_digest_t inventory_digest) override
    {
        std::lock_guard lock (mutex);
        log.push_back ("publish");
        ++publish_count;
        if (force_conflict
            || (conflict_on_publish != 0
                && publish_count == conflict_on_publish))
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
          .inventory_digest = inventory_digest,
          .target_owner = std::move (target_owner)};
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
    int publish_count = 0;
    int conflict_on_publish = 0;
};

class memory_aggregate_authority_t final : public aggregate_authority_port_t
{
  public:
    explicit memory_aggregate_authority_t (
      std::shared_ptr<memory_authority_store_t> authority) :
        authority (std::move (authority))
    {
    }

    aggregate_publish_result_t prepare (
      const std::vector<object_ref_t> &sources,
      std::string target_node_id,
      zlink::framework::location_owner_token_t target_owner,
      std::vector<zlink::framework::relocation_capacity_fence_t>
        relocation_capacity_fences,
      std::string relocation_reference,
      std::uint32_t checksum_crc32c,
      inventory_digest_t inventory_digest) override
    {
        std::lock_guard lock (mutex);
        ++prepare_count;
        if (sources.size () < 2 || prepared
            || relocation_capacity_fences.size ()
                 != sources.size ())
            return {aggregate_publish_status_t::conflict, {}, {}};
        pending.clear ();
        for (const auto &source : sources) {
            auto target = source;
            target.node_id = target_node_id;
            ++target.authority_owner_generation;
            pending.push_back (
              {.source = source,
               .target = target,
               .relocation_reference = relocation_reference,
               .checksum_crc32c = checksum_crc32c,
               .inventory_digest = inventory_digest,
               .target_owner = target_owner});
        }
        prepared = true;
        return {
          aggregate_publish_status_t::prepared, {++next_fence},
          pending};
    }

    aggregate_publish_result_t commit (
      aggregate_relocation_fence_t fence) override
    {
        std::lock_guard lock (mutex);
        ++commit_count;
        if (!prepared || fence.value != next_fence)
            return {aggregate_publish_status_t::conflict, fence, {}};
        {
            std::lock_guard authority_lock (authority->mutex);
            for (const auto &reference : pending) {
                authority->rows[authority_key (
                  reference.source.kind, reference.source.key)] =
                  reference;
            }
        }
        prepared = false;
        return {
          aggregate_publish_status_t::committed, fence, pending};
    }

    void abort (aggregate_relocation_fence_t fence) override
    {
        std::lock_guard lock (mutex);
        if (fence.value == next_fence)
            prepared = false;
    }

    std::shared_ptr<memory_authority_store_t> authority;
    std::mutex mutex;
    std::vector<authority_relocation_reference_t> pending;
    std::uint64_t next_fence = 0;
    int prepare_count = 0;
    int commit_count = 0;
    bool prepared = false;
};

class target_preflight_t final : public target_preflight_port_t
{
  public:
    target_preflight_result_t preflight (
      const std::vector<relocation_unit_t> &units) override
    {
        std::function<void ()> callback;
        {
            std::lock_guard lock (mutex);
            ++calls;
            observed_units = units;
            callback = on_preflight;
        }
        if (callback)
            callback ();
        if (status != target_preflight_status_t::eligible)
            return {status, {}};
        target_preflight_result_t result{
          target_preflight_status_t::eligible, {}};
        for (std::size_t index = 0; index != units.size (); ++index) {
            result.units.push_back (
              {.unit = units[index],
               .target_node_id = "node-b",
               .target_owner = {"owner-b", 1},
               .relocation_capacity_fences = [&] {
                   std::vector<zlink::framework::
                                 relocation_capacity_fence_t>
                     fences;
                   for (std::size_t participant = 0;
                        participant
                          != units[index].participants.size ();
                        ++participant)
                       fences.push_back ({
                         "capacity-"
                         + std::to_string (index + 1)
                         + "-"
                         + std::to_string (participant + 1)});
                   return fences;
               } (),
               .encoded_upper_bound = 1024 * 1024,
               .inventory_digest =
                 digest_with (
                   static_cast<std::uint8_t> (index + 10))});
        }
        return result;
    }

    std::mutex mutex;
    std::vector<relocation_unit_t> observed_units;
    std::function<void ()> on_preflight;
    target_preflight_status_t status =
      target_preflight_status_t::eligible;
    int calls = 0;
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

object_ref_t create_spot (
  stateful_object_runtime_t &runtime,
  object_kind_t kind,
  std::string key)
{
    runtime.replace_placement_candidates (
      {{.mesh_name = "mesh",
        .node_id = "node-a",
        .stable_types = {"spot", "actor"},
        .weight = 100,
        .active_capacity = 100,
        .active_count = 0,
        .pending_capacity = 100,
        .pending_count = 0}});
    auto created = runtime.begin_create (
      {.kind = kind,
       .key = std::move (key),
       .stable_type = "spot",
       .mesh_name = std::optional<std::string>{"mesh"},
       .creation_request = {},
       .exclusive = true,
       .instance_intent = kind == object_kind_t::instance_spot});
    if (created.status != create_status_t::reserved
        || runtime.commit_create (created.attempt) != stateful_error_t::none)
        throw std::runtime_error ("spot creation failed");
    return created.object;
}

inventory_digest_t digest_with (std::uint8_t value)
{
    inventory_digest_t digest{};
    digest.fill (value);
    return digest;
}

void test_generation_barrier_quiesces_yield_spot_and_timer (
  test_context_t &test)
{
    stateful_object_runtime_t objects (16, 8);
    const auto actor = create_actor (objects, "barrier-actor");
    const auto spot =
      create_spot (objects, object_kind_t::user_spot, "barrier-spot");
    test.require (
      objects.register_timer (actor, {9, 1000, 1000, 30})
        == stateful_error_t::none,
      "barrier test timer must register");
    test.require (
      objects.enqueue (
        actor, turn_domain_t::application, {10, {10}})
          == stateful_error_t::none
        && objects.enqueue (
             actor, turn_domain_t::application, {20, {20}})
             == stateful_error_t::none
        && objects.enqueue (
             spot, turn_domain_t::application, {40, {40}})
             == stateful_error_t::none,
      "barrier test turns must enqueue");

    const auto [actor_claim_error, actor_claim] =
      objects.try_claim (actor, turn_domain_t::application);
    const auto [spot_claim_error, spot_claim] =
      objects.try_claim (spot, turn_domain_t::application);
    test.require (
      actor_claim_error == stateful_error_t::none && actor_claim
        && actor_claim->sequence == 10
        && spot_claim_error == stateful_error_t::none && spot_claim
        && spot_claim->sequence == 40,
      "Actor and Spot lanes must both be active before sealing");
    test.require (
      objects.yield_claim (actor, {11, {11}})
        == stateful_error_t::none,
      "yield must retain the Actor claim until its continuation completes");

    std::atomic<bool> seal_completed = false;
    stateful_error_t seal_error = stateful_error_t::conflict;
    aggregate_relocation_seal_t seal;
    std::thread sealing ([&] {
        auto result =
          objects.try_seal_relocation_aggregate ({actor, spot});
        seal_error = result.first;
        seal = std::move (result.second);
        seal_completed.store (true, std::memory_order_release);
    });

    bool sealed = false;
    for (int attempt = 0; attempt != 1000; ++attempt) {
        if (objects.cancel_timer (actor, 999)
            == stateful_error_t::moving) {
            sealed = true;
            break;
        }
        std::this_thread::yield ();
    }
    test.require (
      sealed && !seal_completed.load (std::memory_order_acquire),
      "seal must close timer admission and wait for active lanes");
    test.require (
      objects.enqueue_timer_tick (actor, 9, {30})
        == stateful_error_t::moving,
      "timer dispatch must not mutate the sealed generation");

    const auto [continuation_error, continuation] =
      objects.try_claim (actor, turn_domain_t::application);
    test.require (
      continuation_error == stateful_error_t::none && continuation
        && continuation->sequence == 11,
      "yielded continuation must reacquire its Actor lane while sealed");
    test.require (
      objects.complete_claim (actor, turn_domain_t::application)
          == stateful_error_t::none
        && !seal_completed.load (std::memory_order_acquire),
      "Spot lane must also quiesce before capture");
    test.require (
      objects.complete_claim (spot, turn_domain_t::application)
        == stateful_error_t::none,
      "active Spot lane must complete");
    sealing.join ();

    test.require (
      seal_error == stateful_error_t::none
        && seal.participants.size () == 2
        && seal.participants[0].pending_application.size () == 1
        && seal.participants[0].pending_application[0].sequence == 20
        && seal.participants[0].timers
             == std::vector<logical_timer_t> ({{9, 1000, 1000, 30}}),
      "capture must occur after quiescence and preserve queued work and timers");

    test.require (
      objects.enqueue (
        actor, turn_domain_t::application, {21, {21}})
        == stateful_error_t::none,
      "sealed ingress must be retained for same-generation abort");
    test.require (
      objects.abort_relocation (seal.token) == stateful_error_t::none,
      "same-generation abort must reopen the seal");
    test.require (
      objects.commit_relocation_aggregate (seal.token, "node-b").first
        == stateful_error_t::not_found,
      "stale commit must not mutate an aborted generation");

    const auto [first_error, first] =
      objects.try_claim (actor, turn_domain_t::application);
    test.require (
      first_error == stateful_error_t::none && first
        && first->sequence == 20,
      "abort must restore the captured queue before held ingress");
    test.require (
      objects.complete_claim (actor, turn_domain_t::application)
        == stateful_error_t::none,
      "restored captured turn must complete");
    const auto [held_error, held] =
      objects.try_claim (actor, turn_domain_t::application);
    test.require (
      held_error == stateful_error_t::none && held
        && held->sequence == 21,
      "abort must restore held ingress in FIFO order");
    test.require (
      objects.complete_claim (actor, turn_domain_t::application)
        == stateful_error_t::none,
      "restored held turn must complete");

    const auto [second_error, second_seal] =
      objects.try_seal_relocation_aggregate ({actor, spot});
    test.require (
      second_error == stateful_error_t::none
        && objects.abort_relocation (seal.token)
             == stateful_error_t::not_found,
      "stale abort must not reopen a newer generation");
    const auto [commit_error, committed] =
      objects.commit_relocation_aggregate (second_seal.token, "node-b");
    test.require (
      commit_error == stateful_error_t::none && committed.size () == 2
        && objects.enqueue (
             actor, turn_domain_t::application, {22, {22}})
             == stateful_error_t::generation_stale,
      "post-commit ingress using the source generation must be fenced");
}

void test_close_barrier_waits_and_abort_restores_ingress (
  test_context_t &test)
{
    stateful_object_runtime_t objects (8, 4);
    const auto spot =
      create_spot (objects, object_kind_t::user_spot, "closing-spot");
    test.require (
      objects.enqueue (
        spot, turn_domain_t::application, {1, {1}})
        == stateful_error_t::none,
      "close barrier test turn must enqueue");
    const auto [claim_error, claim] =
      objects.try_claim (spot, turn_domain_t::application);
    test.require (
      claim_error == stateful_error_t::none && claim
        && claim->sequence == 1,
      "Spot lane must be active before close");

    std::atomic<bool> close_completed = false;
    stateful_error_t close_error = stateful_error_t::conflict;
    std::optional<spot_close_token_t> close_token;
    std::thread closing ([&] {
        auto result = objects.begin_close_spot (spot);
        close_error = result.first;
        close_token = std::move (result.second);
        close_completed.store (true, std::memory_order_release);
    });

    bool sealed = false;
    for (int attempt = 0; attempt != 1000; ++attempt) {
        if (objects.register_timer (spot, {1, 1000, 1000, 1})
            == stateful_error_t::moving) {
            sealed = true;
            break;
        }
        std::this_thread::yield ();
    }
    test.require (
      sealed && !close_completed.load (std::memory_order_acquire),
      "close must seal timer admission and wait for the active Spot lane");
    test.require (
      objects.enqueue (
        spot, turn_domain_t::application, {2, {2}})
        == stateful_error_t::none,
      "application ingress during close must be retained");
    test.require (
      objects.complete_claim (spot, turn_domain_t::application)
        == stateful_error_t::none,
      "active Spot lane must complete before close continues");
    closing.join ();
    test.require (
      close_error == stateful_error_t::none && close_token,
      "close must return its generation token after quiescence");
    test.require (
      objects.abort_close_spot (*close_token) == stateful_error_t::none
        && objects.commit_close_spot (*close_token)
             == stateful_error_t::generation_stale,
      "only the current close generation may reopen or commit");
    const auto [held_error, held] =
      objects.try_claim (spot, turn_domain_t::application);
    test.require (
      held_error == stateful_error_t::none && held
        && held->sequence == 2,
      "close abort must restore held ingress");
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

    auto excessive_count = encoded;
    constexpr std::size_t pending_count_offset = 59;
    excessive_count[pending_count_offset] = 0;
    excessive_count[pending_count_offset + 1] = 0;
    excessive_count[pending_count_offset + 2] = 0x10;
    excessive_count[pending_count_offset + 3] = 0x01;
    test.require (
      !maintenance_runtime_t::decode (excessive_count),
      "decoder must reject pending counts above the explicit maximum");

    auto duplicate_sequence = encoded;
    constexpr std::size_t first_sequence_offset = 63;
    constexpr std::size_t second_sequence_offset = 77;
    std::copy_n (
      duplicate_sequence.begin ()
        + static_cast<std::ptrdiff_t> (first_sequence_offset),
      8,
      duplicate_sequence.begin ()
        + static_cast<std::ptrdiff_t> (second_sequence_offset));
    test.require (
      !maintenance_runtime_t::decode (duplicate_sequence),
      "decoder must reject duplicate or unordered queue sequences");

    auto unordered = frozen;
    unordered.pending_application[1].sequence = 1;
    test.require (
      maintenance_runtime_t::encode (unordered, digest).empty (),
      "encoder must reject duplicate or unordered queue sequences");
}

void test_aggregate_envelope_and_crash_recovery (test_context_t &test)
{
    stateful_object_runtime_t source;
    const auto spot =
      create_spot (source, object_kind_t::user_spot, "spot-aggregate");
    const auto actor = create_actor (source, "actor-aggregate");
    const auto [join_error, join] =
      source.begin_membership_move (actor, spot);
    const auto [commit_error, joined_actor] =
      source.commit_membership_move (join);
    test.require (
      join_error == stateful_error_t::none
        && commit_error == stateful_error_t::none,
      "aggregate setup must join the Actor to the User Spot");
    (void) source.enqueue (
      spot, turn_domain_t::application, {1, {10}});
    (void) source.enqueue (
      joined_actor, turn_domain_t::application, {2, {20}});
    (void) source.register_timer (spot, {11, 100, 25, 3});
    (void) source.register_timer (
      joined_actor, {12, 200, 0, 4});

    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    auto aggregates =
      std::make_shared<memory_aggregate_authority_t> (authority);
    auto targets = std::make_shared<target_preflight_t> ();
    maintenance_runtime_t coordinator (
      source,
      maintenance_provider_set_t{
        authority, aggregates, roots, targets});
    const auto digest = digest_with (0x6a);
    const std::vector<object_ref_t> participants{spot, joined_actor};
    const auto moved = coordinator.relocate_aggregate (
      participants, "node-b", {"owner-b", 7},
      {{"capacity-spot"}, {"capacity-actor"}},
      1024 * 1024, digest);
    test.require (
      moved.terminal == relocation_terminal_t::completed
        && moved.authority.size () == 2,
      "aggregate authority commit must publish every participant");

    const auto root =
      roots->get (moved.authority.front ().relocation_reference);
    const auto decoded =
      root ? maintenance_runtime_t::decode_aggregate (*root)
           : std::nullopt;
    test.require (
      decoded && decoded->first.size () == 2
        && decoded->second == digest,
      "aggregate envelope must decode every participant and digest");
    if (root) {
        auto excessive_participants = *root;
        excessive_participants[4] = 0;
        excessive_participants[5] = 0;
        excessive_participants[6] = 0x04;
        excessive_participants[7] = 0x01;
        test.require (
          !maintenance_runtime_t::decode_aggregate (
            excessive_participants),
          "aggregate decoder must reject participant counts above the explicit maximum");
    }

    stateful_object_runtime_t recovered;
    const auto recovery =
      coordinator.recover_aggregate (participants, recovered);
    test.require (
      recovery.terminal == relocation_terminal_t::recovery_required
        && recovery.reason == relocation_reason_t::restore_failed
        && recovery.authority.size () == 2,
      "materialized aggregate must remain recovery-required until lifecycle and ACK completion");

    const auto target_spot =
      authority->read (object_kind_t::user_spot, "spot-aggregate");
    const auto target_actor =
      authority->read (object_kind_t::actor, "actor-aggregate");
    test.require (
      target_spot && target_actor
        && recovered.pending (
             target_spot->target, turn_domain_t::application)
             == 1
        && recovered.pending (
             target_actor->target, turn_domain_t::application)
             == 1
        && recovered.timers (target_spot->target).size () == 1
        && recovered.timers (target_actor->target).size () == 1,
      "aggregate recovery must restore each queue and logical timer");
    test.require (
      target_actor
        && recovered.actor_membership (target_actor->target)
             == std::optional<std::string>{"spot-aggregate"},
      "aggregate recovery must restore canonical User Spot membership");
    const auto staged = recovered.inventory ();
    test.require (
      staged.size () == 2
        && std::all_of (
          staged.begin (), staged.end (),
          [] (const object_inventory_t &entry) {
              return entry.state == object_state_t::recovering;
          }),
      "recovered participants must remain admission-sealed");
    if (target_actor) {
        const auto [claim_error, claim] =
          recovered.try_claim (
            target_actor->target, turn_domain_t::application);
        test.require (
          claim_error == stateful_error_t::moving && !claim,
          "staged recovery must not expose application replay as ready");
        test.require (
          recovered.enqueue_timer_tick (
            target_actor->target, 12, {99})
            == stateful_error_t::moving,
          "staged recovery must not start logical timers before completion");
    }

    const auto repeated =
      coordinator.recover_aggregate (participants, recovered);
    test.require (
      repeated.terminal == relocation_terminal_t::recovery_required
        && recovered.inventory ().size () == 2,
      "exact staged recovery retry must remain idempotent and fail closed");

    if (decoded && target_spot && target_actor) {
        std::vector<object_ref_t> restore_targets{
          target_spot->target, target_actor->target};
        const auto wrong_root =
          recovered.restore_relocation_aggregate (
            decoded->first, restore_targets,
            {"wrong-root",
             moved.authority.front ().checksum_crc32c,
             digest});
        test.require (
          wrong_root == stateful_error_t::conflict,
          "same refs with a different root identity must not be idempotent");

        auto wrong_payload = decoded->first;
        wrong_payload.front ().stable_type = "different-type";
        const auto partial_state =
          recovered.restore_relocation_aggregate (
            std::move (wrong_payload), std::move (restore_targets),
            {moved.authority.front ().relocation_reference,
             moved.authority.front ().checksum_crc32c,
             digest});
        test.require (
          partial_state == stateful_error_t::conflict,
          "same refs with different restored state must not be idempotent");

        const auto spot_frozen = std::find_if (
          decoded->first.begin (), decoded->first.end (),
          [] (const frozen_object_state_t &participant) {
              return participant.owner.kind
                     == object_kind_t::user_spot;
          });
        stateful_object_runtime_t partial;
        const auto partial_seed =
          spot_frozen == decoded->first.end ()
            ? stateful_error_t::invalid
            : partial.restore_relocation (
                *spot_frozen, target_spot->target,
                {moved.authority.front ().relocation_reference,
                 moved.authority.front ().checksum_crc32c,
                 digest});
        const auto partial_retry =
          coordinator.recover_aggregate (participants, partial);
        test.require (
          partial_seed == stateful_error_t::none
            && partial_retry.terminal
                 == relocation_terminal_t::recovery_required
            && partial.inventory ().size () == 1,
          "partial same-ref restore must not add missing participants or report completion");
    }

    if (target_actor) {
        authority->rows[authority_key (
          object_kind_t::actor, "actor-aggregate")]
          .relocation_reference = "different-root";
    }
    stateful_object_runtime_t rejected;
    const auto inconsistent =
      coordinator.recover_aggregate (participants, rejected);
    test.require (
      inconsistent.terminal == relocation_terminal_t::data_lost
        && inconsistent.reason
             == relocation_reason_t::inventory_mismatch
        && rejected.inventory ().empty (),
      "inconsistent aggregate authority must not partially restore");
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
      actor, "node-b", {"owner-b", 1},
      {"capacity-durable"},
      1024 * 1024, digest_with (1));
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
      actor, "node-b", {"owner-b", 1},
      {"capacity-conflict"},
      1024 * 1024, digest_with (2));
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
      actor, "node-b", {"owner-b", 1},
      {"capacity-recovery"},
      1024 * 1024, digest_with (3));
    test.require (moved.authority.has_value (),
                  "published relocation must expose recovery authority");

    stateful_object_runtime_t recovered;
    const auto recovery = coordinator.recover (
      object_kind_t::actor, "actor-recovery", recovered);
    test.require (
      recovery.terminal == relocation_terminal_t::recovery_required
        && recovery.reason == relocation_reason_t::restore_failed,
      "published root must remain staged until lifecycle and ACK completion");
    test.require (
      moved.authority
        && recovered.pending (
             moved.authority->target, turn_domain_t::application)
             == 1
        && recovered.timers (moved.authority->target).size () == 1,
      "recovery must restore queue and logical timer state");
    if (moved.authority) {
        const auto [claim_error, claim] =
          recovered.try_claim (
            moved.authority->target, turn_domain_t::application);
        test.require (
          claim_error == stateful_error_t::moving && !claim,
          "single recovery must keep application admission sealed");
    }

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
          first, "node-b", {"owner-b", 1},
          {"capacity-first"},
          1024, digest_with (4));
    });
    {
        std::unique_lock lock (gate);
        changed.wait (lock, [&] { return entered; });
    }
    const auto second_result = runtime.relocate (
      second, "node-b", {"owner-b", 1},
      {"capacity-second"},
      1024, digest_with (5));
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

void test_host_preflight_is_all_or_none (test_context_t &test)
{
    stateful_object_runtime_t objects;
    const auto actor = create_actor (objects, "preflight-actor");
    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    auto aggregates =
      std::make_shared<memory_aggregate_authority_t> (authority);
    auto targets = std::make_shared<target_preflight_t> ();
    targets->status = target_preflight_status_t::target_unavailable;
    maintenance_runtime_t relocation (
      objects,
      maintenance_provider_set_t{
        authority, aggregates, roots, targets});
    stream_session_registry_t sessions (
      [&] (const std::string &key) {
          return objects.find (object_kind_t::actor, key);
      });
    host_maintenance_runtime_t host (
      objects, sessions, relocation, targets);
    host.mark_serving ();
    const auto result = host.terminate (termination_intent_t::retire);
    test.require (
      result
        == termination_result_t{
          termination_intent_t::retire,
          termination_outcome_t::blocked,
          termination_reason_t::target_unavailable},
      "one target blocker must reject the whole host preflight");
    test.require (
      roots->roots.empty ()
        && objects.enqueue (
             actor, turn_domain_t::application, {1, {1}})
             == stateful_error_t::none,
      "failed preflight must not seal an object or write relocation data");
    const auto after_blocked = objects.begin_create (
      {.kind = object_kind_t::actor,
       .key = "after-blocked",
       .stable_type = "actor",
       .mesh_name = std::optional<std::string>{"mesh"},
       .creation_request = {},
       .exclusive = true,
       .instance_intent = false});
    test.require (
      host.state () == host_runtime_state_t::serving
        && !host.terminal_result ()
        && after_blocked.status == create_status_t::reserved,
      "blocked Retire must restore Serving without a host terminal result");
}

void test_user_spot_aggregate_and_stream_barrier (
  test_context_t &test)
{
    stateful_object_runtime_t objects;
    const auto spot =
      create_spot (objects, object_kind_t::user_spot, "user-spot");
    const auto actor = create_actor (objects, "member-actor");
    const auto [move_error, move] =
      objects.begin_membership_move (actor, spot);
    const auto [commit_error, joined] =
      objects.commit_membership_move (move);
    test.require (
      move_error == stateful_error_t::none
        && commit_error == stateful_error_t::none
        && joined == actor,
      "test actor must join the User Spot before inventory");

    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    auto aggregates =
      std::make_shared<memory_aggregate_authority_t> (authority);
    auto targets = std::make_shared<target_preflight_t> ();
    bool preflight_before_seal = false;
    bool structural_admission_sealed = false;
    targets->on_preflight = [&] {
        preflight_before_seal =
          objects.enqueue (
            actor, turn_domain_t::application, {7, {7}})
          == stateful_error_t::none;
        const auto create = objects.begin_create (
          {.kind = object_kind_t::actor,
           .key = "late-actor",
           .stable_type = "actor",
           .mesh_name = std::optional<std::string>{"mesh"},
           .creation_request = {},
           .exclusive = true,
           .instance_intent = false});
        structural_admission_sealed =
          create.status == create_status_t::failed
          && create.error == stateful_error_t::moving;
    };
    maintenance_runtime_t relocation (
      objects,
      maintenance_provider_set_t{
        authority, aggregates, roots, targets});
    stream_session_registry_t sessions (
      [&] (const std::string &key) {
          return objects.find (object_kind_t::actor, key);
      });
    const auto connection = sessions.open ("stream-a");
    const auto [bind_error, binding] =
      sessions.bind (connection, actor);
    test.require (
      bind_error == stateful_error_t::none,
      "bound STREAM session setup must succeed");

    int terminal_observations = 0;
    host_maintenance_runtime_t host (
      objects, sessions, relocation, targets,
      [&] (const termination_result_t &) {
          ++terminal_observations;
      });
    host.mark_serving ();
    const auto result = host.terminate (termination_intent_t::retire);
    test.require (
      result
        == termination_result_t{
          termination_intent_t::retire,
          termination_outcome_t::stopped,
          termination_reason_t::none},
      "eligible User Spot aggregate Retire must stop normally");
    test.require (
      preflight_before_seal && structural_admission_sealed,
      "preflight must keep existing queues open while structural inventory is sealed");
    test.require (
      aggregates->prepare_count == 1
        && aggregates->commit_count == 1
        && aggregates->pending.size () == 2,
      "User Spot and its member Actor must use one aggregate commit");
    test.require (
      targets->observed_units.size () == 1
        && targets->observed_units.front ().participants.size () == 2,
      "preflight inventory must expose one bounded User Spot aggregate");
    test.require (
      !sessions.is_current (binding),
      "owner commit must fence the old STREAM binding generation");
    const auto [stale_error, stale_dispatch] =
      sessions.admit_inbound (binding);
    test.require (
      stale_error != stateful_error_t::none && !stale_dispatch,
      "old STREAM packets must not pass after the route barrier commits");
    test.require (
      terminal_observations == 1
        && host.terminal_result ().has_value (),
      "host terminal observation and stored result must complete once");
}

void test_shutdown_wins_during_retire_preflight (
  test_context_t &test)
{
    stateful_object_runtime_t objects;
    (void) create_actor (objects, "race-actor");
    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    auto aggregates =
      std::make_shared<memory_aggregate_authority_t> (authority);
    auto targets = std::make_shared<target_preflight_t> ();
    std::mutex gate;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    targets->on_preflight = [&] {
        std::unique_lock lock (gate);
        entered = true;
        changed.notify_all ();
        changed.wait (lock, [&] { return release; });
    };
    maintenance_runtime_t relocation (
      objects,
      maintenance_provider_set_t{
        authority, aggregates, roots, targets});
    stream_session_registry_t sessions (
      [&] (const std::string &key) {
          return objects.find (object_kind_t::actor, key);
      });
    int observations = 0;
    host_maintenance_runtime_t host (
      objects, sessions, relocation, targets,
      [&] (const termination_result_t &) { ++observations; });
    host.mark_serving ();

    termination_result_t retire_result;
    termination_result_t shutdown_result;
    std::thread retire ([&] {
        retire_result = host.terminate (termination_intent_t::retire);
    });
    {
        std::unique_lock lock (gate);
        changed.wait (lock, [&] { return entered; });
    }
    std::thread shutdown ([&] {
        shutdown_result =
          host.terminate (termination_intent_t::shutdown);
    });
    while (host.intent_snapshot ()
           != std::optional<termination_intent_t>{
             termination_intent_t::shutdown}) {
        std::this_thread::yield ();
    }
    {
        std::lock_guard lock (gate);
        release = true;
    }
    changed.notify_all ();
    retire.join ();
    shutdown.join ();
    const termination_result_t expected{
      termination_intent_t::shutdown,
      termination_outcome_t::stopped,
      termination_reason_t::none};
    test.require (
      retire_result == expected && shutdown_result == expected,
      "Shutdown seal claim during Retire preflight must win for all waiters");
    test.require (
      roots->roots.empty () && aggregates->commit_count == 0,
      "winning Shutdown must not start continuity relocation");
    test.require (
      observations == 1,
      "first-intent shared operation must emit one terminal observation");
}

void test_post_commit_failure_is_force_stopped (
  test_context_t &test)
{
    stateful_object_runtime_t objects;
    (void) create_actor (objects, "commit-first");
    (void) create_actor (objects, "conflict-second");
    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    authority->conflict_on_publish = 2;
    auto aggregates =
      std::make_shared<memory_aggregate_authority_t> (authority);
    auto targets = std::make_shared<target_preflight_t> ();
    maintenance_runtime_t relocation (
      objects,
      maintenance_provider_set_t{
        authority, aggregates, roots, targets});
    stream_session_registry_t sessions (
      [&] (const std::string &key) {
          return objects.find (object_kind_t::actor, key);
      });
    host_maintenance_runtime_t host (
      objects, sessions, relocation, targets);
    host.mark_serving ();
    const auto result = host.terminate (termination_intent_t::retire);
    test.require (
      result
        == termination_result_t{
          termination_intent_t::retire,
          termination_outcome_t::force_stopped,
          termination_reason_t::relocation_failed},
      "failure after one authority commit must not return Blocked");
    test.require (
      host.state () == host_runtime_state_t::stopped
        && host.terminal_result () == result,
      "postcommit failure must finish bounded teardown in Stopped");
}

void test_public_relocation_store_adapter (test_context_t &test)
{
    auto public_store =
      std::make_shared<public_memory_relocation_store_t> ();
    public_relocation_store_adapter_t adapter (public_store);
    const std::vector<std::uint8_t> payload{0, 1, 127, 255};
    const auto stored = adapter.put (payload, std::chrono::hours (24));
    test.require (
      stored.reference == "public-root"
        && stored.checksum_crc32c
             == maintenance_runtime_t::crc32c (payload),
      "public relocation adapter must preserve reference and CRC32C");
    test.require (
      adapter.get (stored.reference)
        == std::optional<std::vector<std::uint8_t>>{payload},
      "public relocation adapter must preserve immutable payload bytes");
    adapter.remove (stored.reference);
    test.require (
      !adapter.get (stored.reference),
      "public relocation adapter must map delete and missing results");
}

void test_public_authority_store_adapter (test_context_t &test)
{
    public_memory_authority_store_t store;
    store.snapshot =
      zlink::framework::authority_snapshot_t{
        "1",
        {std::byte{0x01}},
        7,
        11,
        {"owner-a", 3},
        std::chrono::system_clock::now ()};
    public_authority_store_adapter_t adapter (store);
    const object_ref_t source{
      object_kind_t::actor,
      "actor-public",
      7,
      11,
      "mesh",
      "node-a"};
    const zlink::framework::location_owner_token_t target_owner{
      "owner-b", 5};
    const auto published = adapter.publish (
      source, "node-b", target_owner, {"capacity-public"},
      "root-public", 42,
      digest_with (9));
    test.require (
      published.status == authority_publish_status_t::published
        && published.current
        && published.current->target.node_id == "node-b"
        && published.current->target.authority_owner_generation == 12,
      "public authority adapter must publish exact NewOwner generation");
    test.require (
      store.observed_target_owner
        && store.observed_target_owner->owner_id == "owner-b"
        && store.observed_target_owner->lease_generation == 5
        && store.observed_capacity_fence
        && store.observed_capacity_fence->value
             == "capacity-public"
        && store.snapshot->owner.owner_id == "owner-b",
      "public authority adapter must pass exact target owner and capacity fence");
    const auto read =
      adapter.read (object_kind_t::actor, "actor-public");
    test.require (
      read && read->relocation_reference == "root-public"
        && read->checksum_crc32c == 42
        && read->inventory_digest == digest_with (9)
        && read->target_owner.lease_generation == 5,
      "public authority adapter must decode only its Framework-owned payload");

    const std::vector<std::byte> application_payload{
      std::byte{0x31}, std::byte{0x32}};
    store.snapshot =
      zlink::framework::authority_snapshot_t{
        "10",
        application_payload,
        7,
        12,
        {"owner-b", 5},
        std::chrono::system_clock::now ()};
    const auto completion_published =
      adapter.publish_completion (
        object_kind_t::actor,
        "actor-public",
        "mesh",
        7,
        "completion-prepared",
        51);
    const auto completion_replaced =
      adapter.replace_completion (
        object_kind_t::actor,
        "actor-public",
        7,
        "completion-prepared",
        51,
        "completion-delivered",
        52);
    const auto completion_read =
      adapter.read (
        object_kind_t::actor,
        "actor-public");
    test.require (
      completion_published.status
          == authority_publish_status_t::published
        && completion_replaced.status
             == authority_publish_status_t::published
        && completion_read
        && completion_read->relocation_reference
             == "completion-delivered"
        && completion_read->checksum_crc32c == 52
        && completion_read->source.object_generation == 7
        && completion_read->source.authority_owner_generation
             == 12,
      "completion cursor roots must use exact preserve-generation authority CAS");
    const auto completion_released =
      adapter.release_completion (
        object_kind_t::actor,
        "actor-public",
        7,
        "completion-delivered",
        52);
    test.require (
      completion_released
        && store.snapshot
        && store.snapshot->payload == application_payload
        && store.snapshot->authority_owner_generation == 12
        && !adapter.read (
          object_kind_t::actor,
          "actor-public"),
      "Delivered release must restore authority payload before root cleanup");
}

void test_durable_join_completion_replacement_and_ordering (
  test_context_t &test)
{
    auto store = std::make_shared<memory_relocation_store_t> ();
    durable_join_completion_store_t source (store);
    const object_ref_t actor{
      object_kind_t::actor, "actor-join", 7, 12,
      "mesh", "node-b"};
    auto root = source.prepare (
      durable_join_completion_record_t{
        0x1111, 0x2222, actor, {4, 5, 6},
        join_completion_cursor_t::prepared});
    root = source.commit (root);

    std::vector<std::string> events;
    const auto failed_root = source.deliver (
      root, actor,
      [&] (const durable_join_completion_record_t &record) {
          events.push_back ("callback-failed");
          test.require (
            record.operation_id_high == 0x1111
              && record.operation_id_low == 0x2222
              && record.raw_reply
                   == std::vector<std::uint8_t> ({4, 5, 6}),
            "replacement callback must retain operation id and raw reply");
          return false;
      });
    test.require (
      failed_root.reference == root.reference,
      "failed callback must retain the committed immutable root");

    durable_join_completion_store_t replacement (store);
    int delivered = 0;
    root = replacement.deliver (
      failed_root, actor,
      [&] (const durable_join_completion_record_t &) {
          ++delivered;
          events.push_back ("callback-delivered");
          return true;
      });
    events.push_back ("backlog");
    const auto deduplicated = replacement.deliver (
      root, actor,
      [&] (const durable_join_completion_record_t &) {
          ++delivered;
          return true;
      });
    test.require (
      delivered == 1
        && events
             == std::vector<std::string> (
               {"callback-failed", "callback-delivered", "backlog"})
        && deduplicated.reference == root.reference,
      "replacement must deliver once before opening backlog");

    auto stale = actor;
    ++stale.object_generation;
    bool fenced = false;
    try {
        (void) replacement.deliver (root, stale, {});
    }
    catch (const std::invalid_argument &) {
        fenced = true;
    }
    test.require (
      fenced,
      "replacement must reject a mismatched Actor generation");
    replacement.cleanup (root);
    test.require (
      !store->get (root.reference),
      "delivered Join completion root must be removed after cleanup");
}

} // namespace
void test_production_relocation_restore_and_replay_vertical (
  test_context_t &test)
{
    namespace mesh = zlink::framework::runtime::mesh;
    namespace protocol = zlink::framework::runtime::protocol;
    using namespace std::chrono_literals;

    const auto bytes = [] (const std::string &value) {
        return std::vector<std::uint8_t> (value.begin (), value.end ());
    };
    const auto descriptor = [&] (const std::string &rid) {
        return mesh::service_node_descriptor_t{
          "mesh", bytes (rid), 1, 1, "tcp://127.0.0.1:0", {},
          mesh::service_node_state_t::preparing};
    };

    mesh::raw_mesh_node_owner_t source_transport (
      {descriptor ("maintenance-source")});
    mesh::raw_mesh_node_owner_t target_transport (
      {descriptor ("maintenance-target")});
    source_transport.start ();
    target_transport.start ();
    const auto source_descriptor =
      source_transport.topology ().local_descriptor ();
    const auto target_descriptor =
      target_transport.topology ().local_descriptor ();
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    test.require (
      source_transport.connect_peer (
        target_transport.endpoint (), target_descriptor),
      "production relocation vertical must connect source to target");
    while ((!source_transport.topology ().peer (
               target_descriptor.node_routing_id)
            || !target_transport.topology ().peer (
              source_descriptor.node_routing_id))
           && std::chrono::steady_clock::now () < deadline) {
        const auto now = mesh::service_liveness_registry_t::clock_t::now ();
        (void) source_transport.drain_monitor_events (now);
        (void) target_transport.drain_monitor_events (now);
        (void) source_transport.pump_one (now);
        (void) target_transport.pump_one (now);
        std::this_thread::yield ();
    }
    test.require (
      source_transport.topology ().peer (
        target_descriptor.node_routing_id).has_value ()
        && target_transport.topology ().peer (
          source_descriptor.node_routing_id).has_value (),
      "production relocation vertical requires two Ready owners");

    stateful_object_runtime_t source_objects;
    stateful_object_runtime_t target_objects;
    const auto actor = create_actor (
      source_objects, "production-replay-actor",
      "maintenance-source");

    protocol::frozen_application_record_t accepted;
    accepted.kind = protocol::frozen_record_kind_t::actor_request;
    accepted.source_kind = protocol::frozen_source_kind_t::node;
    accepted.source = {
      "source-owner", 17, source_descriptor.node_routing_id,
      source_descriptor.lifecycle_generation};
    accepted.operation = {
      0x1111222233334444ULL, 0x5555666677778888ULL};
    accepted.operation_kind = 4;
    accepted.reply_route_id = 77;
    accepted.body = protocol::frozen_actor_application_body_t{
      {actor.key, actor.object_generation,
       source_descriptor.node_routing_id,
       source_descriptor.lifecycle_generation,
       actor.authority_owner_generation, 19},
      {"ActorPacket", "application/json", bytes ("accepted")}};

    const auto canonical =
      protocol::encode_frozen_application_record (accepted);
    test.require (
      source_objects.enqueue (
        actor, turn_domain_t::application,
        {1, protocol::encode_frozen_record (canonical)})
        == stateful_error_t::none,
      "production relocation vertical must queue a canonical accepted request");

    raw_relocation_replay_coordinator_t source_wire (source_transport);
    raw_relocation_replay_coordinator_t target_wire (target_transport);
    auto roots = std::make_shared<memory_relocation_store_t> ();
    auto authority = std::make_shared<memory_authority_store_t> ();
    maintenance_runtime_t maintenance (
      source_objects, authority, roots);
    maintenance.attach_relocation_wire (source_wire);

    const protocol::relocation_id_t relocation{301, 302};
    const protocol::relocation_coordinator_fence_t coordinator{
      "coordinator-owner", 23, bytes ("coordinator-rid"), 29,
      "authority-store-version"};
    object_ref_t target_actor = actor;
    target_actor.node_id = "maintenance-target";
    ++target_actor.authority_owner_generation;

    int target_restore = 0;
    int target_stage = 0;
    int target_abort = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> acknowledged;
    eligible_relocation_unit_t::canonical_wire_context_t wire_context{
      .relocation = relocation,
      .target_attempt_generation = 31,
      .coordinator = coordinator,
      .target_node_routing_id = target_descriptor.node_routing_id,
      .target_node_generation = target_descriptor.lifecycle_generation,
      .participant_ids = {1},
      .prepare_target =
        [&] (const std::vector<frozen_object_state_t> &participants,
             const std::vector<protocol::relocation_data_t> &records) {
            if (participants.size () != 1 || records.size () != 1
                || !records.front ().frozen_record)
                return false;
            const auto &frozen = *records.front ().frozen_record;
            if (frozen.source != accepted.source
                || frozen.operation != accepted.operation
                || frozen.reply_route_id != accepted.reply_route_id)
                return false;

            auto restored = participants.front ();
            restored.pending_application.clear ();
            const auto restore_error = target_objects.restore_relocation (
              std::move (restored), target_actor,
              {"production-restore", 1, digest_with (0x31)});
            if (restore_error != stateful_error_t::none
                && restore_error != stateful_error_t::already_exists)
                return false;
            ++target_restore;

            return target_wire.register_target ({
              relocation, 31, coordinator, 1, accepted.source,
              records.front ().object,
              [&] (const protocol::relocation_data_t &record) {
                  if (!record.frozen_record)
                      return false;
                  ++target_stage;
                  return target_objects.enqueue (
                           target_actor, turn_domain_t::application,
                           {record.sequence,
                            protocol::encode_frozen_record (
                              *record.frozen_record)})
                         == stateful_error_t::none;
              }});
        },
      .acknowledged =
        [&] (std::uint64_t participant, std::uint64_t high_water) {
            acknowledged.emplace_back (participant, high_water);
        },
      .abort_target = [&] { ++target_abort; }};

    const auto result = maintenance.relocate (
      actor, "maintenance-target", {"target-owner", 37},
      {"capacity-fence"}, 1024 * 1024, digest_with (0x31),
      wire_context);
    test.require (
      result.terminal == relocation_terminal_t::completed
        && result.replay_records.size () == 1
        && target_restore == 1 && target_abort == 0,
      "maintenance must prepare target Restore and retain one replay record");

    raw_relocation_replay_result_t target_result =
      raw_relocation_replay_result_t::no_data;
    raw_relocation_replay_result_t source_result =
      raw_relocation_replay_result_t::no_data;
    while ((target_result == raw_relocation_replay_result_t::no_data
            || source_result == raw_relocation_replay_result_t::no_data)
           && std::chrono::steady_clock::now () < deadline) {
        const auto now = mesh::service_liveness_registry_t::clock_t::now ();
        if (target_result == raw_relocation_replay_result_t::no_data) {
            (void) target_transport.pump_one (now);
            target_result = target_wire.pump_one ();
        }
        if (source_result == raw_relocation_replay_result_t::no_data) {
            (void) source_transport.pump_one (now);
            source_result = source_wire.pump_one ();
        }
        std::this_thread::yield ();
    }
    test.require (
      target_result == raw_relocation_replay_result_t::applied
        && source_result
             == raw_relocation_replay_result_t::ack_advanced
        && target_stage == 1
        && acknowledged
             == std::vector<std::pair<std::uint64_t, std::uint64_t>>{
               {1, 1}}
        && target_objects.pending (
             target_actor, turn_domain_t::application) == 1,
      "command 31/32 must stage the exact request and persist monotonic ACK");

    source_transport.close ();
    target_transport.close ();
}

int main ()
{
    test_context_t test;
    test_generation_barrier_quiesces_yield_spot_and_timer (test);
    test_close_barrier_waits_and_abort_restores_ingress (test);
    test_envelope_round_trip (test);
    test_aggregate_envelope_and_crash_recovery (test);
    test_publication_and_handoff (test);
    test_conflict_aborts_without_losing_ingress (test);
    test_recovery_and_data_loss (test);
    test_permit_precedes_seal (test);
    test_host_preflight_is_all_or_none (test);
    test_user_spot_aggregate_and_stream_barrier (test);
    test_shutdown_wins_during_retire_preflight (test);
    test_post_commit_failure_is_force_stopped (test);
    test_public_relocation_store_adapter (test);
    test_public_authority_store_adapter (test);
    test_durable_join_completion_replacement_and_ordering (test);
    test_production_relocation_restore_and_replay_vertical (test);
    return test.failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
