/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "core/ctx.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "sockets/socket_base.hpp"
#include "services/spot/spot_runtime.hpp"

#include <string.h>
#include <thread>
#include <vector>

namespace
{
void set_zero_linger (zlink::socket_base_t *socket_)
{
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &zero, sizeof (zero)));
}

void set_subscribe_all (zlink::socket_base_t *socket_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0));
}

void set_recv_timeout (zlink::socket_base_t *socket_, int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (socket_->setsockopt (
      ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout_ms_, sizeof (timeout_ms_)));
}

bool recv_message (zlink::socket_base_t *socket_,
                   std::vector<std::string> *frames_out_)
{
    frames_out_->clear ();
    zlink::msg_t frame;
    if (frame.init () != 0)
        return false;
    if (socket_->recv (&frame, 0) != 0) {
        frame.close ();
        return false;
    }

    bool more = true;
    while (more) {
        frames_out_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        more = (frame.flags () & zlink::msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
        if (frame.init () != 0)
            return false;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return false;
        }
    }
    return true;
}

void close_socket (zlink::ctx_t *ctx_, zlink::socket_base_t *&socket_)
{
    (void) ctx_;
    if (!socket_)
        return;
    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
}

zlink::spot_topic_batch_bucket_t make_bucket (
  const char *topic_, const char *m1_, const char *m2_ = NULL,
  const char *m3_ = NULL, const char *m4_ = NULL, const char *m5_ = NULL)
{
    zlink::spot_topic_batch_bucket_t bucket;
    bucket.topic = topic_;
    const char *payloads[] = {m1_, m2_, m3_, m4_, m5_};
    for (size_t i = 0; i < sizeof (payloads) / sizeof (payloads[0]); ++i) {
        if (!payloads[i])
            continue;
        zlink::spot_topic_batch_message_t message;
        message.parts.push_back (payloads[i]);
        message.payload_bytes =
          zlink::spot_data_plane_forwarder_t::logical_message_payload_bytes (
            message.parts);
        message.encoded_bytes =
          zlink::spot_data_plane_forwarder_t::logical_message_encoded_bytes (
            message.parts);
        bucket.pending_message_count += 1;
        bucket.pending_payload_bytes += message.payload_bytes;
        bucket.pending_encoded_bytes += message.encoded_bytes + 4;
        bucket.messages.push_back (message);
    }
    return bucket;
}

void test_mesh_xsub_monitor_ready_zero_clears_connected_peer ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_ready_peer_count.store (1);
    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 0;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000", sizeof (raw.remote_addr) - 1);

    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_EQUAL_UINT (
      1, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (
      1, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (0, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_ready_count_growth_marks_connected ()
{
    zlink::spot_runtime_t runtime (NULL);
    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000", sizeof (raw.remote_addr) - 1);

    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_EQUAL_UINT (
      1, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (
      1, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_disconnect_clears_connected_peer ()
{
    zlink::spot_runtime_t runtime (NULL);
    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000", sizeof (raw.remote_addr) - 1);

    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);

    raw.event = ZLINK_EVENT_DISCONNECTED;
    raw.value = 0;
    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_TRUE (runtime.mesh_peer_state.connected_endpoints.empty ());
    TEST_ASSERT_EQUAL_UINT (
      0, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (2, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_ready_count_changes_without_rewriting_membership ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9001");
    runtime.mesh_peer_state.connected_ready_peer_count.store (2);

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9001", sizeof (raw.remote_addr) - 1);

    bool endpoint_membership_changed = true;
    TEST_ASSERT_FALSE (zlink::sync_mesh_peer_monitor_state (
      &runtime.mesh_peer_state, raw, &endpoint_membership_changed));
    TEST_ASSERT_EQUAL_UINT (
      2, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (
      2, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_FALSE (endpoint_membership_changed);
    TEST_ASSERT_EQUAL_UINT64 (0, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_ready_growth_reports_endpoint_membership_change ()
{
    zlink::spot_mesh_peer_state_t state;

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000",
             sizeof (raw.remote_addr) - 1);

    bool endpoint_membership_changed = false;
    TEST_ASSERT_TRUE (
      zlink::sync_mesh_peer_monitor_state (&state, raw,
                                           &endpoint_membership_changed));
    TEST_ASSERT_TRUE (endpoint_membership_changed);
    TEST_ASSERT_EQUAL_UINT (1, state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (1, state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, state.version.load ());
}

void test_mesh_xsub_monitor_ready_positive_keeps_endpoint_present ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9000");

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9001",
             sizeof (raw.remote_addr) - 1);

    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_EQUAL_UINT (
      2, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_TRUE (runtime.mesh_peer_state.connected_endpoints.count (
                        "wss://127.0.0.1:9001")
                      == 1);
    TEST_ASSERT_EQUAL_UINT (
      2, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_same_ready_count_does_not_bump_version ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9001");

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 2;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9002",
             sizeof (raw.remote_addr) - 1);

    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_EQUAL_UINT (
      3, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (
      3, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, runtime.mesh_peer_state.version.load ());
}

void test_explicit_disconnect_updates_private_mesh_peer_state ()
{
    zlink::spot_mesh_peer_state_t state;
    state.connected_endpoints.insert ("wss://127.0.0.1:9000");
    state.connected_endpoints.insert ("wss://127.0.0.1:9001");
    state.connected_ready_peer_count.store (2);

    TEST_ASSERT_TRUE (zlink::remove_connected_mesh_peer_endpoint (
      &state, "wss://127.0.0.1:9000"));
    TEST_ASSERT_EQUAL_UINT (1, state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (2, state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, state.version.load ());

    TEST_ASSERT_TRUE (zlink::remove_connected_mesh_peer_endpoint (
      &state, "wss://127.0.0.1:9001"));
    TEST_ASSERT_TRUE (state.connected_endpoints.empty ());
    TEST_ASSERT_EQUAL_UINT (0, state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (2, state.version.load ());
}

void test_ready_endpoint_helper_tracks_endpoint_local_readiness ()
{
    std::set<std::string> endpoints;
    endpoints.insert ("wss://127.0.0.1:9000");
    endpoints.insert ("wss://127.0.0.1:9001");

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));

    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9001",
             sizeof (raw.remote_addr) - 1);
    TEST_ASSERT_FALSE (
      zlink::sync_monitor_ready_endpoint (&endpoints, raw));
    TEST_ASSERT_EQUAL_UINT (2, endpoints.size ());
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9001") == 1);

    raw.value = 0;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000",
             sizeof (raw.remote_addr) - 1);
    TEST_ASSERT_FALSE (
      zlink::sync_monitor_ready_endpoint (&endpoints, raw));
    TEST_ASSERT_EQUAL_UINT (2, endpoints.size ());
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9001") == 1);
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9000") == 1);

    raw.value = 2;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9002",
             sizeof (raw.remote_addr) - 1);
    TEST_ASSERT_TRUE (
      zlink::sync_monitor_ready_endpoint (&endpoints, raw));
    TEST_ASSERT_EQUAL_UINT (3, endpoints.size ());
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9001") == 1);
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9000") == 1);
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9002") == 1);
}

void test_bootstrap_descriptor_republish_stops_after_ready_topology_stabilizes ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("tls://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_ready_peer_count.store (1);
    runtime.mesh_peer_state.version.store (7);

    TEST_ASSERT_TRUE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, false, 7));
    TEST_ASSERT_TRUE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, UINT64_MAX));
    TEST_ASSERT_FALSE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, 7));
    TEST_ASSERT_FALSE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, 7));
}

void test_bootstrap_descriptor_republish_resumes_after_topology_change ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("tls://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_ready_peer_count.store (1);
    runtime.mesh_peer_state.version.store (3);

    TEST_ASSERT_FALSE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, 3));

    runtime.mesh_peer_state.version.store (4);
    TEST_ASSERT_TRUE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, 3));
}

void test_batch_decoder_strict_header_detection_and_false_positive_fallback ()
{
    zlink::spot_data_plane_protocol_state_t state;
    bool is_batch = true;
    std::vector<std::string> frames;
    frames.push_back ("not-a-batch");
    frames.push_back ("metadata");
    frames.push_back ("body");

    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_protocol_t::decode_batch_frame (
      "A", frames, &state, &is_batch));
    TEST_ASSERT_FALSE (is_batch);
    TEST_ASSERT_FALSE (state.pending_unbatch.active);

    zlink::spot_topic_batch_bucket_t bucket = make_bucket ("A", "m1");
    std::vector<std::string> batch_frames;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_data_plane_forwarder_t::encode_batch_frames (
        bucket, &batch_frames));
    batch_frames.push_back ("extra");
    is_batch = true;
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_protocol_t::decode_batch_frame (
      "A", batch_frames, &state, &is_batch));
    TEST_ASSERT_FALSE (is_batch);
}

void test_batch_decoder_rejects_malformed_encoded_bytes ()
{
    zlink::spot_topic_batch_bucket_t bucket = make_bucket ("A", "m1");
    std::vector<std::string> frames;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_data_plane_forwarder_t::encode_batch_frames (
        bucket, &frames));
    reinterpret_cast<unsigned char &> (frames[1][8]) ^= 0x01;

    zlink::spot_data_plane_protocol_state_t state;
    bool is_batch = false;
    TEST_ASSERT_FAILURE_ERRNO (
      EBADMSG, zlink::spot_data_plane_protocol_t::decode_batch_frame (
                  "A", frames, &state, &is_batch));
    TEST_ASSERT_TRUE (is_batch);
    TEST_ASSERT_FALSE (state.pending_unbatch.active);
}

void test_batch_resume_unbatches_across_multiple_turns ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    zlink::socket_base_t *fanout = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (fanout);
    TEST_ASSERT_NOT_NULL (sink);
    set_zero_linger (fanout);
    set_zero_linger (sink);
    set_recv_timeout (sink, 50);
    TEST_ASSERT_SUCCESS_ERRNO (fanout->bind ("inproc://spot-unbatch-out"));
    TEST_ASSERT_SUCCESS_ERRNO (sink->connect ("inproc://spot-unbatch-out"));
    std::this_thread::sleep_for (std::chrono::milliseconds (10));

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.unbatch_max_messages_per_turn = 2;
    runtime.peer_batch_config.unbatch_max_bytes_per_turn = 1024;
    zlink::spot_topic_batch_bucket_t bucket =
      make_bucket ("A", "m1", "m2", "m3", "m4", "m5");
    std::vector<std::string> frames;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_data_plane_forwarder_t::encode_batch_frames (
        bucket, &frames));

    zlink::spot_data_plane_protocol_state_t state;
    bool is_batch = false;
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_protocol_t::decode_batch_frame (
      "A", frames, &state, &is_batch));
    TEST_ASSERT_TRUE (is_batch);
    TEST_ASSERT_TRUE (state.pending_unbatch.active);

    std::vector<std::string> delivered;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_data_plane_protocol_t::resume_pending_unbatch (
        fanout, &runtime, &state));
    TEST_ASSERT_TRUE (recv_message (sink, &delivered));
    TEST_ASSERT_EQUAL_STRING ("m1", delivered[1].c_str ());
    TEST_ASSERT_TRUE (recv_message (sink, &delivered));
    TEST_ASSERT_EQUAL_STRING ("m2", delivered[1].c_str ());
    TEST_ASSERT_TRUE (state.pending_unbatch.active);
    TEST_ASSERT_EQUAL_UINT32 (2, state.pending_unbatch.decoded_message_index);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_data_plane_protocol_t::resume_pending_unbatch (
        fanout, &runtime, &state));
    TEST_ASSERT_TRUE (recv_message (sink, &delivered));
    TEST_ASSERT_EQUAL_STRING ("m3", delivered[1].c_str ());
    TEST_ASSERT_TRUE (recv_message (sink, &delivered));
    TEST_ASSERT_EQUAL_STRING ("m4", delivered[1].c_str ());
    TEST_ASSERT_TRUE (state.pending_unbatch.active);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_data_plane_protocol_t::resume_pending_unbatch (
        fanout, &runtime, &state));
    TEST_ASSERT_TRUE (recv_message (sink, &delivered));
    TEST_ASSERT_EQUAL_STRING ("m5", delivered[1].c_str ());
    TEST_ASSERT_FALSE (state.pending_unbatch.active);

    close_socket (ctx, sink);
    close_socket (ctx, fanout);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

}

int main (int argc, char **argv)
{
    UNITY_BEGIN ();
    RUN_TEST (test_mesh_xsub_monitor_ready_zero_clears_connected_peer);
    RUN_TEST (test_mesh_xsub_monitor_ready_count_growth_marks_connected);
    RUN_TEST (test_mesh_xsub_monitor_disconnect_clears_connected_peer);
    RUN_TEST (
      test_mesh_xsub_monitor_ready_count_changes_without_rewriting_membership);
    RUN_TEST (
      test_mesh_xsub_monitor_ready_growth_reports_endpoint_membership_change);
    RUN_TEST (test_mesh_xsub_monitor_ready_positive_keeps_endpoint_present);
    RUN_TEST (test_mesh_xsub_monitor_same_ready_count_does_not_bump_version);
    RUN_TEST (test_explicit_disconnect_updates_private_mesh_peer_state);
    RUN_TEST (test_ready_endpoint_helper_tracks_endpoint_local_readiness);
    RUN_TEST (
      test_bootstrap_descriptor_republish_stops_after_ready_topology_stabilizes);
    RUN_TEST (test_bootstrap_descriptor_republish_resumes_after_topology_change);
    RUN_TEST (
      test_batch_decoder_strict_header_detection_and_false_positive_fallback);
    RUN_TEST (test_batch_decoder_rejects_malformed_encoded_bytes);
    RUN_TEST (test_batch_resume_unbatches_across_multiple_turns);
    return UNITY_END ();
}
