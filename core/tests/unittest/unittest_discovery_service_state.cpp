/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
class counting_discovery_observer_t : public zlink::discovery_observer_t
{
  public:
    counting_discovery_observer_t () : update_count (0) {}

    void on_service_update (const std::string &service_name_)
    {
        ++update_count;
        last_service = service_name_;
    }

    int update_count;
    std::string last_service;
};

static zlink::provider_info_t make_provider (const char *service_name_,
                                             const char *endpoint_,
                                             uint16_t service_role_,
                                             int64_t value_,
                                             unsigned char metadata0_,
                                             unsigned char metadata1_)
{
    zlink::provider_info_t provider;
    provider.auto_connect_type = ZLINK_AUTO_CONNECT_CLIENT_SERVER;
    provider.channel_name = service_name_;
    provider.endpoint = endpoint_;
    provider.service_role = service_role_;
    provider.value = value_;
    provider.routing_id.size = 0;
    provider.metadata.push_back (metadata0_);
    provider.metadata.push_back (metadata1_);
    provider.registered_at = 0;
    return provider;
}

void test_discovery_service_state_tracks_provider_views_and_sequences ()
{
    zlink::discovery_service_state_t state;
    std::vector<zlink::provider_info_t> providers;
    providers.push_back (make_provider ("svc", "tcp://local", 1, 7, 1, 2));
    providers.push_back (make_provider ("svc", "tcp://remote", 2, 9, 3, 4));

    zlink::discovery_service_change_t change;
    zlink_routing_id_t routing_id;
    memset (&routing_id, 0, sizeof (routing_id));
    state.apply_provider_snapshot (11, 1, providers, "svc", routing_id,
                                   &change);

    TEST_ASSERT_TRUE (change.changed);
    TEST_ASSERT_EQUAL_UINT (ZLINK_DISCOVERY_SERVICE_UP, change.event.event_type);
    TEST_ASSERT_EQUAL_UINT32 (2, change.event.value);
    TEST_ASSERT_EQUAL_UINT64 (1, state.update_seq ());
    TEST_ASSERT_EQUAL_UINT64 (1, state.service_update_seq ());

    std::set<zlink::discovery_member_key_t> local_members;
    local_members.insert (
      zlink::discovery_member_key_t (1, std::string ("tcp://local")));

    std::vector<zlink_member_peer_entry_t> peers;
    state.snapshot_member_peers (ZLINK_AUTO_CONNECT_CLIENT_SERVER, local_members,
                                 &peers);
    TEST_ASSERT_EQUAL_UINT (1, peers.size ());
    TEST_ASSERT_EQUAL_UINT16 (2, peers[0].service_role);
    TEST_ASSERT_EQUAL_STRING ("svc", peers[0].channel_name);
    TEST_ASSERT_EQUAL_STRING ("tcp://remote", peers[0].endpoint);
    TEST_ASSERT_EQUAL_INT64 (9, peers[0].value);

    TEST_ASSERT_EQUAL_UINT (1, peers.size ());
}

void test_discovery_service_state_blocks_observer_removal_while_inflight ()
{
    zlink::discovery_service_state_t state;
    counting_discovery_observer_t observer;

    TEST_ASSERT_SUCCESS_ERRNO (state.add_observer (&observer));

    std::set<std::string> changed;
    changed.insert ("svc");

    std::vector<zlink::discovery_observer_t *> observers;
    state.begin_observer_notification ("svc", changed, &observers);
    TEST_ASSERT_EQUAL_UINT (1, observers.size ());

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, state.remove_observer (&observer));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);

    observers[0]->on_service_update ("svc");
    state.finish_observer_notification (observers.size ());

    TEST_ASSERT_EQUAL_INT (1, observer.update_count);
    TEST_ASSERT_EQUAL_STRING ("svc", observer.last_service.c_str ());
    TEST_ASSERT_SUCCESS_ERRNO (state.remove_observer (&observer));
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_discovery_service_state_tracks_provider_views_and_sequences);
    RUN_TEST (test_discovery_service_state_blocks_observer_removal_while_inflight);
    return UNITY_END ();
}
