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
void *create_spot_sub_handle (void *node_);
int destroy_spot_node_with_handles (void **node_p_);
typedef zlink_submit_result_t (*spot_publish_fn_t) (void *,
                                                    const char *,
                                                    zlink_msg_t *,
                                                    size_t,
                                                    zlink_send_flags_t);
int publish_text (spot_publish_fn_t publish_fn_,
                  void *handle_,
                  const char *topic_id_,
                  const char *payload_,
                  int flags_);

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
bool wait_for_spot_recv_message (void *spot_sub_,
                                 const char *expected_topic_,
                                 const char *expected_payload_,
                                 size_t expected_payload_size_,
                                 int timeout_ms_);
bool wait_for_node_message (void *node_,
                            const char *expected_topic_,
                            const char *expected_payload_,
                            size_t expected_payload_size_,
                            int timeout_ms_);

bool wait_for_spot_node_ready_state (
  void *node_,
  int role_,
  zlink_monitor_state_mask_t required_flags_,
  uint32_t min_ready_peer_count_,
  int timeout_ms_);

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

void run_spot_peer_tcp_test ();
void run_spot_peer_reverse_tcp_test ();

void test_spot_peer_tcp ();
void test_spot_peer_tcp_reverse_publish ();
void test_spot_multi_publisher ();
void test_spot_aggregate_subscription_refcount ();
void test_spot_sub_handler_basic ();
void test_spot_recv_callback_isolated_by_handle ();
void test_spot_node_direct_local_and_child_interop ();
void test_spot_node_pub_ingress_local_spot_subscribe_surface ();
void test_spot_node_discovery_direct_and_child_interop ();

#endif
