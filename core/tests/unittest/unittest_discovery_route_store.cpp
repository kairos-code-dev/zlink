/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "core/ctx.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_runtime_base.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/clock.hpp"
#include "utils/mutex.hpp"

#include <chrono>
#include <map>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include <unity.h>
#include <vector>

#define private public
#include "services/registry/registry.hpp"
#undef private

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
zlink_routing_id_t make_rid (const char *rid_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = strlen (rid_);
    memcpy (rid.data, rid_, rid.size);
    return rid;
}

zlink::registry_t::owner_identity_t make_owner (const char *channel_,
                                                const char *rid_,
                                                uint32_t source_registry_,
                                                uint64_t registration_id_)
{
    zlink::registry_t::owner_identity_t owner;
    owner.channel_name = channel_;
    owner.service_role = ZLINK_SERVICE_ROLE_ROUTER;
    owner.routing_id_key = rid_;
    owner.source_registry = source_registry_;
    owner.registration_id = registration_id_;
    return owner;
}

zlink::registry_t::route_key_t make_route_key (const char *channel_,
                                               const char *key_)
{
    zlink::registry_t::route_key_t route_key;
    route_key.channel_name = channel_;
    route_key.kind = 1;
    route_key.key = key_;
    return route_key;
}

zlink::registry_t::route_entry_t make_route (
  const zlink::registry_t::route_key_t &route_key_,
  const zlink::registry_t::owner_identity_t &owner_,
  const char *value_,
  uint64_t updated_at_ms_,
  uint32_t advertising_registry_)
{
    zlink::registry_t::route_entry_t route;
    route.key = route_key_;
    route.owner = owner_;
    route.value.assign (value_, value_ + strlen (value_));
    route.updated_at_ms = updated_at_ms_;
    route.advertising_registry = advertising_registry_;
    return route;
}

void add_live_provider (zlink::registry_t *registry_,
                        const zlink::registry_t::owner_identity_t &owner_)
{
    zlink::registry_t::service_key_t service_key;
    service_key.channel_name = owner_.channel_name;
    zlink::registry_t::service_entry_t &service =
      registry_->_projection_state.services[service_key];
    service.auto_connect_type = ZLINK_AUTO_CONNECT_ROUTE_MESH;

    zlink::registry_t::provider_key_t provider_key;
    provider_key.service_role = owner_.service_role;
    provider_key.endpoint = std::string ("inproc://") + owner_.routing_id_key;

    zlink::registry_t::provider_entry_t &provider =
      service.providers[provider_key];
    provider.service_role = owner_.service_role;
    provider.endpoint = provider_key.endpoint;
    provider.routing_id = make_rid (owner_.routing_id_key.c_str ());
    provider.source_registry = owner_.source_registry;
    provider.registration_id = owner_.registration_id;
}

void test_pending_route_materializes_when_owner_provider_arrives ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    zlink::registry_t registry (static_cast<zlink::ctx_t *> (ctx));

    const zlink::registry_t::route_key_t key =
      make_route_key ("route-store", "actor-1");
    const zlink::registry_t::owner_identity_t owner =
      make_owner ("route-store", "owner-a", 7, 11);
    const zlink::registry_t::route_entry_t route =
      make_route (key, owner, "owner-a-value", 100, 2);

    zlink::registry_t::route_key_set_t dirty;
    registry.upsert_route_observation_locked (route, &dirty);
    registry.materialize_dirty_routes_locked (dirty);
    TEST_ASSERT_TRUE (registry._projection_state.routes.find (key) == registry._projection_state.routes.end ());

    add_live_provider (&registry, owner);
    registry.promote_owner_route_records_locked (owner);

    zlink::registry_t::route_map_t::const_iterator materialized =
      registry._projection_state.routes.find (key);
    TEST_ASSERT_TRUE (materialized != registry._projection_state.routes.end ());
    TEST_ASSERT_EQUAL_STRING ("owner-a",
                              materialized->owner.routing_id_key.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_route_winner_falls_back_to_remaining_observation ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    zlink::registry_t registry (static_cast<zlink::ctx_t *> (ctx));

    const zlink::registry_t::route_key_t key =
      make_route_key ("route-store", "actor-2");
    const zlink::registry_t::owner_identity_t old_owner =
      make_owner ("route-store", "owner-old", 1, 10);
    const zlink::registry_t::owner_identity_t new_owner =
      make_owner ("route-store", "owner-new", 2, 20);
    add_live_provider (&registry, old_owner);
    add_live_provider (&registry, new_owner);

    zlink::registry_t::route_key_set_t dirty;
    registry.upsert_route_observation_locked (
      make_route (key, old_owner, "old", 100, 1), &dirty);
    registry.upsert_route_observation_locked (
      make_route (key, new_owner, "new", 200, 2), &dirty);
    registry.materialize_dirty_routes_locked (dirty);

    TEST_ASSERT_EQUAL_STRING (
      "owner-new", registry._projection_state.routes.find (key)->owner.routing_id_key.c_str ());

    registry.cleanup_owner_records_locked (new_owner, 300);

    zlink::registry_t::route_map_t::const_iterator materialized =
      registry._projection_state.routes.find (key);
    TEST_ASSERT_TRUE (materialized != registry._projection_state.routes.end ());
    TEST_ASSERT_EQUAL_STRING ("owner-old",
                              materialized->owner.routing_id_key.c_str ());
    TEST_ASSERT_EQUAL_UINT64 (1, registry._projection_state.route_stats.owner_cleanup_count);
    TEST_ASSERT_EQUAL_UINT64 (
      1, registry._projection_state.route_stats.owner_cleanup_observation_visits);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_route_memory_budget_rejects_new_observation_without_partial_store ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    zlink::registry_t registry (static_cast<zlink::ctx_t *> (ctx));
    registry._projection_state.route_limits.memory_budget_bytes = 1;

    const zlink::registry_t::route_key_t key =
      make_route_key ("route-store", "actor-3");
    const zlink::registry_t::owner_identity_t owner =
      make_owner ("route-store", "owner-budget", 1, 30);
    const zlink::registry_t::route_entry_t route =
      make_route (key, owner, "too-large", 100, 1);

    int route_error = 0;
    TEST_ASSERT_FALSE (
      registry.route_store_can_fit_locked (route, 0, &route_error));
    TEST_ASSERT_EQUAL_INT (ENOSPC, route_error);
    TEST_ASSERT_EQUAL_UINT64 (0, registry._projection_state.route_observations.size ());
    TEST_ASSERT_EQUAL_UINT64 (0, registry._projection_state.routes.size ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_provider_rid_collision_is_excluded_from_materialized_peers ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    zlink::registry_t registry (static_cast<zlink::ctx_t *> (ctx));

    zlink::registry_t::owner_identity_t first =
      make_owner ("provider-collision", "same-rid", 1, 10);
    zlink::registry_t::owner_identity_t second =
      make_owner ("provider-collision", "same-rid", 2, 20);
    add_live_provider (&registry, first);
    add_live_provider (&registry, second);
    zlink::registry_t::service_key_t service_key;
    service_key.channel_name = "provider-collision";
    zlink::registry_t::provider_key_t second_endpoint_key;
    second_endpoint_key.service_role = second.service_role;
    second_endpoint_key.endpoint = "inproc://same-rid-conflict";
    zlink::registry_t::provider_entry_t &provider =
      registry._projection_state.services[service_key].providers[second_endpoint_key];
    provider.service_role = second.service_role;
    provider.endpoint = second_endpoint_key.endpoint;
    provider.routing_id = make_rid ("same-rid");
    provider.source_registry = second.source_registry;
    provider.registration_id = second.registration_id;

    size_t count = 4;
    zlink_member_peer_entry_t entries[4];
    memset (entries, 0, sizeof (entries));
    TEST_ASSERT_SUCCESS_ERRNO (
      registry.member_peers ("provider-collision", entries, &count));
    TEST_ASSERT_EQUAL_UINT64 (0, count);

    TEST_ASSERT_FALSE (registry.owner_is_live_locked (first));
    TEST_ASSERT_FALSE (registry.owner_is_live_locked (second));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_route_store_large_lookup_and_cleanup_stays_fast ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    zlink::registry_t registry (static_cast<zlink::ctx_t *> (ctx));

    const size_t record_count = 65536;
    const zlink::registry_t::owner_identity_t owner =
      make_owner ("route-store-perf", "owner-perf", 1, 100);
    add_live_provider (&registry, owner);

    std::vector<zlink::registry_t::route_key_t> keys;
    keys.reserve (record_count);
    zlink::registry_t::route_key_set_t dirty;

    const std::chrono::steady_clock::time_point insert_start =
      std::chrono::steady_clock::now ();
    for (size_t i = 0; i < record_count; ++i) {
        const std::string key = std::string ("actor-") + std::to_string (i);
        keys.push_back (make_route_key ("route-store-perf", key.c_str ()));
        const std::string value = std::string ("value-") + std::to_string (i);
        registry.upsert_route_observation_locked (
          make_route (keys.back (), owner, value.c_str (), i + 1, 1), &dirty);
    }
    registry.materialize_dirty_routes_locked (dirty);
    const std::chrono::steady_clock::time_point insert_end =
      std::chrono::steady_clock::now ();

    TEST_ASSERT_EQUAL_UINT64 (record_count, registry._projection_state.route_observations.size ());
    TEST_ASSERT_EQUAL_UINT64 (record_count, registry._projection_state.routes.size ());
    TEST_ASSERT_TRUE (registry._projection_state.routes.rehash_step_count () > 0);
    TEST_ASSERT_TRUE (registry._projection_state.routes.max_chain_length () < 128);

    size_t found = 0;
    const std::chrono::steady_clock::time_point lookup_start =
      std::chrono::steady_clock::now ();
    for (size_t i = 0; i < keys.size (); ++i) {
        zlink::registry_t::route_map_t::const_iterator it =
          registry._projection_state.routes.find (keys[i]);
        if (it != registry._projection_state.routes.end ())
            ++found;
    }
    const std::chrono::steady_clock::time_point lookup_end =
      std::chrono::steady_clock::now ();
    TEST_ASSERT_EQUAL_UINT64 (record_count, found);

    const std::chrono::steady_clock::time_point cleanup_start =
      std::chrono::steady_clock::now ();
    registry.cleanup_owner_records_locked (owner, 200);
    const std::chrono::steady_clock::time_point cleanup_end =
      std::chrono::steady_clock::now ();

    TEST_ASSERT_EQUAL_UINT64 (0, registry._projection_state.route_observations.size ());
    TEST_ASSERT_EQUAL_UINT64 (0, registry._projection_state.routes.size ());
    TEST_ASSERT_EQUAL_UINT64 (
      record_count, registry._projection_state.route_stats.owner_cleanup_observation_visits);

    const long long insert_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (insert_end
                                                            - insert_start)
        .count ();
    const long long lookup_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (lookup_end
                                                            - lookup_start)
        .count ();
    const long long cleanup_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (cleanup_end
                                                            - cleanup_start)
        .count ();

    printf ("route_store_perf records=%zu insert_materialize_ms=%lld "
            "lookup_ms=%lld cleanup_ms=%lld rehash_steps=%llu "
            "max_chain=%zu\n",
            record_count,
            insert_ms,
            lookup_ms,
            cleanup_ms,
            static_cast<unsigned long long> (
              registry._projection_state.routes.rehash_step_count ()),
            registry._projection_state.routes.max_chain_length ());

    TEST_ASSERT_TRUE (insert_ms < 30000);
    TEST_ASSERT_TRUE (lookup_ms < 5000);
    TEST_ASSERT_TRUE (cleanup_ms < 30000);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_route_table_pauses_rehash_during_snapshot_iteration ()
{
    zlink::registry_t::route_map_t table;
    std::vector<zlink::registry_t::route_key_t> keys;

    for (size_t i = 0; i < 10000 && !table.is_rehashing (); ++i) {
        const std::string key = std::string ("pause-") + std::to_string (i);
        keys.push_back (make_route_key ("route-pause", key.c_str ()));
        table[keys.back ()] = zlink::registry_t::route_entry_t ();
    }

    TEST_ASSERT_TRUE (table.is_rehashing ());
    const uint64_t steps_before = table.rehash_step_count ();
    {
        zlink::registry_t::route_map_t::rehash_pause_guard_t pause (table);
        TEST_ASSERT_TRUE (table.rehash_paused ());
        for (size_t i = 0; i < keys.size (); ++i)
            (void) table.find (keys[i]);
        TEST_ASSERT_EQUAL_UINT64 (steps_before, table.rehash_step_count ());
    }
    TEST_ASSERT_FALSE (table.rehash_paused ());

    for (size_t i = 0;
         i < 1000 && table.is_rehashing ()
         && table.rehash_step_count () == steps_before;
         ++i) {
        (void) table.find (keys[i % keys.size ()]);
    }
    TEST_ASSERT_TRUE (!table.is_rehashing ()
                      || table.rehash_step_count () > steps_before);
}

void test_route_table_snapshot_values_uses_cursor_chunks ()
{
    zlink::registry_t::route_map_t table;
    const size_t record_count = 257;
    for (size_t i = 0; i < record_count; ++i) {
        const std::string key = std::string ("chunk-") + std::to_string (i);
        zlink::registry_t::route_key_t route_key =
          make_route_key ("route-snapshot-cursor", key.c_str ());
        zlink::registry_t::route_entry_t entry;
        entry.key = route_key;
        table[route_key] = entry;
    }

    size_t cursor = 0;
    size_t emitted = 0;
    size_t chunks = 0;
    while (emitted < record_count) {
        std::vector<zlink::registry_t::route_entry_t> routes;
        zlink::registry_t::route_map_t::rehash_pause_guard_t pause (table);
        cursor = table.snapshot_values (cursor, 31, &routes);
        TEST_ASSERT_TRUE (routes.size () <= 31);
        TEST_ASSERT_TRUE (!routes.empty ());
        emitted += routes.size ();
        chunks++;
    }
    TEST_ASSERT_EQUAL_UINT64 (record_count, emitted);
    TEST_ASSERT_TRUE (chunks > 1);
}

void test_route_materialized_table_manual_large_perf ()
{
    const char *enabled = getenv ("ZLINK_ROUTE_STORE_10M_PERF");
    if (!enabled || strcmp (enabled, "1") != 0)
        return;

    size_t record_count = 10000000;
    const char *records_env = getenv ("ZLINK_ROUTE_STORE_PERF_RECORDS");
    if (records_env && records_env[0] != '\0') {
        const unsigned long long parsed = strtoull (records_env, NULL, 10);
        if (parsed > 0)
            record_count = static_cast<size_t> (parsed);
    }

    zlink::registry_t::route_map_t table;
    const std::chrono::steady_clock::time_point insert_start =
      std::chrono::steady_clock::now ();
    for (size_t i = 0; i < record_count; ++i) {
        const std::string key = std::string ("large-") + std::to_string (i);
        zlink::registry_t::route_key_t route_key =
          make_route_key ("route-10m", key.c_str ());
        zlink::registry_t::route_entry_t entry;
        entry.key = route_key;
        entry.updated_at_ms = i + 1;
        table[route_key] = entry;
    }
    const std::chrono::steady_clock::time_point insert_end =
      std::chrono::steady_clock::now ();

    TEST_ASSERT_EQUAL_UINT64 (record_count, table.size ());
    TEST_ASSERT_TRUE (table.rehash_step_count () > 0);
    TEST_ASSERT_TRUE (table.max_chain_length () < 128);

    size_t found = 0;
    const size_t lookup_stride = record_count / 100000 == 0
                                   ? 1
                                   : record_count / 100000;
    const std::chrono::steady_clock::time_point lookup_start =
      std::chrono::steady_clock::now ();
    for (size_t i = 0; i < record_count; i += lookup_stride) {
        const std::string key = std::string ("large-") + std::to_string (i);
        zlink::registry_t::route_key_t route_key =
          make_route_key ("route-10m", key.c_str ());
        if (table.find (route_key) != table.end ())
            ++found;
    }
    const std::chrono::steady_clock::time_point lookup_end =
      std::chrono::steady_clock::now ();

    size_t snapshot_count = 0;
    size_t cursor = 0;
    const std::chrono::steady_clock::time_point snapshot_start =
      std::chrono::steady_clock::now ();
    while (snapshot_count < record_count) {
        std::vector<zlink::registry_t::route_entry_t> chunk;
        chunk.reserve (4096);
        zlink::registry_t::route_map_t::rehash_pause_guard_t pause (table);
        cursor = table.snapshot_values (cursor, 4096, &chunk);
        snapshot_count += chunk.size ();
        if (chunk.empty ())
            break;
    }
    const std::chrono::steady_clock::time_point snapshot_end =
      std::chrono::steady_clock::now ();

    const std::chrono::steady_clock::time_point erase_start =
      std::chrono::steady_clock::now ();
    for (size_t i = 0; i < record_count; ++i) {
        const std::string key = std::string ("large-") + std::to_string (i);
        zlink::registry_t::route_key_t route_key =
          make_route_key ("route-10m", key.c_str ());
        table.erase (route_key);
    }
    const std::chrono::steady_clock::time_point erase_end =
      std::chrono::steady_clock::now ();

    TEST_ASSERT_EQUAL_UINT64 ((record_count + lookup_stride - 1)
                                / lookup_stride,
                              found);
    TEST_ASSERT_EQUAL_UINT64 (record_count, snapshot_count);
    TEST_ASSERT_EQUAL_UINT64 (0, table.size ());

    const long long insert_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (insert_end
                                                            - insert_start)
        .count ();
    const long long lookup_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (lookup_end
                                                            - lookup_start)
        .count ();
    const long long snapshot_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (snapshot_end
                                                            - snapshot_start)
        .count ();
    const long long erase_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (erase_end
                                                            - erase_start)
        .count ();
    printf ("route_table_large_perf records=%zu insert_ms=%lld "
            "sample_lookup_ms=%lld snapshot_ms=%lld erase_ms=%lld "
            "rehash_steps=%llu max_chain=%zu sample_lookups=%zu\n",
            record_count,
            insert_ms,
            lookup_ms,
            snapshot_ms,
            erase_ms,
            static_cast<unsigned long long> (table.rehash_step_count ()),
            table.max_chain_length (),
            found);
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_pending_route_materializes_when_owner_provider_arrives);
    RUN_TEST (test_route_winner_falls_back_to_remaining_observation);
    RUN_TEST (test_route_memory_budget_rejects_new_observation_without_partial_store);
    RUN_TEST (test_provider_rid_collision_is_excluded_from_materialized_peers);
    RUN_TEST (test_route_store_large_lookup_and_cleanup_stays_fast);
    RUN_TEST (test_route_table_pauses_rehash_during_snapshot_iteration);
    RUN_TEST (test_route_table_snapshot_values_uses_cursor_chunks);
    RUN_TEST (test_route_materialized_table_manual_large_perf);
    return UNITY_END ();
}
