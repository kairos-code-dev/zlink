/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_TEST_SPOT_PUBSUB_SCENARIO_SHARED_HPP_INCLUDED__
#define __ZLINK_TEST_SPOT_PUBSUB_SCENARIO_SHARED_HPP_INCLUDED__

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct queued_spot_message_t
{
    std::string topic;
    std::vector<std::string> parts;
};

struct queued_spot_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<queued_spot_message_t> messages;
};

struct service_monitor_probe_t
{
    service_monitor_probe_t () : event_count (0) {}

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
    uint64_t event_count;
};

struct callback_probe_t;

bool test_debug_enabled ();
void step_log (const char *msg_);

bool read_spot_snapshot (void *spot_, zlink_monitor_snapshot_t *out_);
int env_int_or_default (const char *name_, int default_value_);

void ignore_spot_handler (const zlink_routing_id_t *,
                          const char *,
                          size_t,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *);
void queued_spot_handler (const zlink_routing_id_t *,
                          const char *topic_,
                          size_t topic_len_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *);

void *create_spot_node (void *ctx_, const char *service_name_);
void *create_spot_pub_handle (void *node_);
void *create_spot_sub_handle (void *node_, zlink_subscribe_handler_fn handler_);
int destroy_spot_node_with_handles (void **node_p_);
typedef int (*spot_publish_fn_t) (void *,
                                  const char *,
                                  zlink_msg_t *,
                                  size_t,
                                  zlink_send_flags_t);
int publish_text (spot_publish_fn_t publish_fn_,
                  void *handle_,
                  const char *topic_id_,
                  const char *payload_,
                  zlink_send_flags_t flags_);
int create_spot_pub_sub (void *node_, void **pub_p, void **sub_p);
int destroy_spot_pub_sub (void **pub_p, void **sub_p);
int set_node_pub_option (void *node_,
                         int option_,
                         const void *optval_,
                         size_t optvallen_);
int set_node_sub_option (void *node_,
                         int option_,
                         const void *optval_,
                         size_t optvallen_);

void close_spot_parts (zlink_msg_t *parts_, size_t part_count_);

queued_spot_probe_t *ensure_queued_spot_probe (void *handle_, bool node_owned_);
void remove_queued_spot_probe (void *handle_, bool node_owned_);
bool pop_next_spot_message (queued_spot_probe_t *probe_,
                            queued_spot_message_t *message_out_);

bool wait_for_spot_message (void *spot_sub_,
                            const char *expected_topic_,
                            const char *expected_payload_,
                            size_t expected_payload_size_,
                            int timeout_ms_);
bool wait_for_node_message (void *node_,
                            const char *expected_topic_,
                            const char *expected_payload_,
                            size_t expected_payload_size_,
                            int timeout_ms_);

bool wait_for_service_event_match (service_monitor_probe_t *probe_,
                                   uint32_t expected_event_type_,
                                   const char *endpoint_prefix_,
                                   const char *subject_,
                                   int min_value_,
                                   zlink_service_event_t *event_out_,
                                   int timeout_ms_);
bool wait_for_service_event (service_monitor_probe_t *probe_,
                             uint32_t expected_event_type_,
                             const char *endpoint_prefix_,
                             int timeout_ms_);

void *open_spot_monitor_with_probe (void *spot_,
                                    zlink_spot_monitor_event_mask_t events_,
                                    service_monitor_probe_t *probe_);
void *open_spot_node_monitor_with_probe (
  void *node_,
  int role_,
  zlink_spot_monitor_event_mask_t events_,
  service_monitor_probe_t *probe_);
int close_service_monitor_with_probe (void **monitor_);

bool wait_for_spot_ready_state (void *spot_,
                                zlink_monitor_state_mask_t required_flags_,
                                uint32_t min_ready_peer_count_,
                                int timeout_ms_);
bool wait_for_spot_node_ready_state (
  void *node_,
  int role_,
  zlink_monitor_state_mask_t required_flags_,
  uint32_t min_ready_peer_count_,
  int timeout_ms_);
bool wait_for_spot_pub_peers (void *pub_, int timeout_ms_);
bool wait_for_spot_sub_peers (void *sub_, int timeout_ms_);

int bind_spot_node_with_port_seed (void *node_,
                                   const char *prefix_,
                                   int *port_seed_,
                                   char *endpoint_out_);
void *create_started_registry_with_port_seed (void *ctx_,
                                              int *port_seed_,
                                              char *pub_endpoint_out_,
                                              size_t pub_size_,
                                              char *router_endpoint_out_,
                                              size_t router_size_);
int connect_discovery_registry_with_retry (void *discovery_,
                                           const char *endpoint_,
                                           int timeout_ms_);

enum peer_transport_t
{
    peer_transport_ipc = 0,
    peer_transport_tcp,
    peer_transport_ws,
    peer_transport_tls,
    peer_transport_wss
};

void run_spot_peer_transport_test (peer_transport_t transport_);

void test_spot_peer_ipc ();
void test_spot_peer_tcp ();
void test_spot_peer_ws ();
void test_spot_peer_tls ();
void test_spot_peer_wss ();
void test_spot_sub_delivery_ready_immediate_first_publish ();
void test_spot_unified_wss_subscription_ready_first_delivery ();
void test_spot_multi_publisher ();
void test_spot_same_handle_concurrent_publish ();
void test_spot_sub_handler_basic ();
void test_spot_recv_callback_isolated_by_handle ();
void test_spot_facade_handler_receives_source_rid ();
void test_spot_recv_callback_isolated_by_service_with_discovery ();
void test_spot_node_direct_local_and_child_interop ();
void test_spot_node_direct_remote_peer_mesh ();
void test_spot_node_direct_sub_option_inheritance_and_handler_conflict ();
void test_spot_node_direct_first_publish_race ();
void test_spot_node_discovery_direct_and_child_interop ();
void test_spot_node_manual_peer_topology_ownership ();
void test_discovery_destroy_invalidates_attached_spot_node_handle ();
void test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery ();

#endif
