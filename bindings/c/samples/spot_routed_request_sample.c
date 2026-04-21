/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

typedef struct
{
    int completed;
    zlink_request_result_t result;
    char reply[64];
    size_t reply_len;
} reply_capture_t;

static void capture_reply (zlink_request_result_t result_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *userdata_)
{
    reply_capture_t *capture = (reply_capture_t *) userdata_;
    assert (capture != NULL);
    capture->completed = 1;
    capture->result = result_;
    capture->reply_len = 0;
    if (result_ != ZLINK_REQUEST_OK) {
        fprintf (stderr, "[spot-routed-request-sample] callback result=%d\n",
                 (int) result_);
    }

    if (result_ != ZLINK_REQUEST_OK || !parts_ || part_count_ == 0)
        return;

    capture->reply_len = zlink_msg_size (&parts_[0]);
    assert (capture->reply_len < sizeof (capture->reply));
    memcpy (capture->reply, zlink_msg_data (&parts_[0]), capture->reply_len);
    capture->reply[capture->reply_len] = '\0';
}

static void init_capture (reply_capture_t *capture_)
{
    memset (capture_, 0, sizeof (*capture_));
    capture_->result = ZLINK_REQUEST_INTERNAL_ERROR;
}

static void init_routing_id (zlink_routing_id_t *rid_, const char *text_)
{
    const size_t len = strlen (text_);
    assert (len > 0 && len <= sizeof (rid_->data));
    memset (rid_, 0, sizeof (*rid_));
    rid_->size = (uint8_t) len;
    memcpy (rid_->data, text_, len);
}

static void pump_requester_progress (void *requester_)
{
    for (;;) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        zlink_recv_result_t rc =
          zlink_spot_recv (requester_, &source_node_rid, &source_spot_rid,
                           &request_seq, &parts, &part_count, ZLINK_DONTWAIT);
        if (rc == ZLINK_RECV_OK) {
            zlink_multipart_close (parts, part_count);
            continue;
        }
        assert (rc == ZLINK_RECV_NO_DATA);
        break;
    }
}

static int drive_spot_to_spot (void *requester_,
                               void *responder_,
                               reply_capture_t *capture_,
                               int timeout_ms_)
{
    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);

    while (!capture_->completed) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        zlink_recv_result_t rc =
          zlink_spot_recv (responder_, &source_node_rid, &source_spot_rid,
                           &request_seq, &parts, &part_count, ZLINK_DONTWAIT);
        if (rc == ZLINK_RECV_OK) {
            zlink_msg_t reply;
            assert (source_node_rid != NULL);
            assert (source_spot_rid != NULL);
            assert (request_seq != 0);
            assert (part_count == 1);
            assert (zlink_msg_size (&parts[0]) == strlen ("spot-ping"));
            make_message (&reply, "spot-pong");
            assert (zlink_spot_reply_spot (
                      responder_, source_node_rid, source_spot_rid, request_seq,
                      &reply, 1)
                    == ZLINK_SUBMIT_OK);
            zlink_multipart_close (parts, part_count);
        }

        pump_requester_progress (requester_);
        if (capture_->completed)
            break;

        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms_)
            return 0;

        sample_pause_ms (1);
    }

    return 1;
}

static void prime_spot_state (void *spot_)
{
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    zlink_recv_result_t rc =
      zlink_spot_recv (spot_, &source_node_rid, &source_spot_rid, &request_seq,
                       &parts, &part_count, ZLINK_DONTWAIT);
    if (rc == ZLINK_RECV_OK)
        zlink_multipart_close (parts, part_count);
    else
        assert (rc == ZLINK_RECV_NO_DATA);
}

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    void *requester_node = zlink_spot_node_new (ctx);
    void *requester = zlink_spot_new (requester_node);
    void *responder = zlink_spot_new (requester_node);
    assert (requester_node != NULL);
    assert (requester != NULL);
    assert (responder != NULL);

    assert (zlink_set_routing_id (requester_node, "sample-requester-node",
                                  strlen ("sample-requester-node"))
            == 0);
    assert (zlink_set_routing_id (requester, "sample-requester-spot",
                                  strlen ("sample-requester-spot"))
            == 0);
    assert (zlink_set_routing_id (responder, "sample-responder-spot",
                                  strlen ("sample-responder-spot"))
            == 0);

    zlink_routing_id_t responder_node_rid;
    zlink_routing_id_t responder_spot_rid;
    init_routing_id (&responder_node_rid, "sample-requester-node");
    init_routing_id (&responder_spot_rid, "sample-responder-spot");

    prime_spot_state (responder);
    reply_capture_t capture;
    init_capture (&capture);

    zlink_msg_t request;
    make_message (&request, "spot-ping");
    assert (zlink_spot_request_spot (
              requester, &responder_node_rid, &responder_spot_rid, &request, 1,
              &capture_reply, &capture, ZLINK_SEND_FLAGS_NONE, 5000)
            == ZLINK_SUBMIT_OK);
    assert (drive_spot_to_spot (requester, responder, &capture, 5000));
    if (capture.result != ZLINK_REQUEST_OK) {
        fprintf (stderr, "[spot-routed-request-sample] spot->spot result=%d\n",
                 (int) capture.result);
    }
    assert (capture.result == ZLINK_REQUEST_OK);
    assert (strcmp (capture.reply, "spot-pong") == 0);

    printf ("[spot/routed/request] spot->spot: \"%s\"\n", "spot-pong");

    zlink_spot_destroy (&responder);
    zlink_spot_destroy (&requester);
    zlink_spot_node_destroy (&requester_node);
    zlink_ctx_term (ctx);
    return 0;
}
