/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

extern int zlink_spot_request_progress_internal (void *spot_);

typedef struct
{
    int completed;
    zlink_request_result_t result;
    char reply[64];
    size_t reply_len;
} reply_capture_t;

typedef struct
{
    int handled;
    void *responder;
} responder_capture_t;

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

static void init_responder_capture (responder_capture_t *capture_,
                                    void *responder_)
{
    memset (capture_, 0, sizeof (*capture_));
    capture_->responder = responder_;
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
    (void) zlink_spot_request_progress_internal (requester_);
}

static void handle_spot_request (const zlink_routing_id_t *source_node_rid_,
                                 const zlink_routing_id_t *source_spot_rid_,
                                 uint64_t request_seq_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 void *userdata_)
{
    responder_capture_t *capture = (responder_capture_t *) userdata_;
    zlink_msg_t reply;

    assert (capture != NULL);
    assert (capture->responder != NULL);
    assert (source_node_rid_ != NULL);
    assert (source_spot_rid_ != NULL);
    assert (request_seq_ != 0);
    assert (part_count_ == 1);
    assert (zlink_msg_size (&parts_[0]) == strlen ("spot-ping"));

    make_message (&reply, "spot-pong");
    assert (zlink_spot_reply_spot (
              capture->responder, source_node_rid_, source_spot_rid_,
              request_seq_, &reply, 1)
            == ZLINK_SUBMIT_OK);
    capture->handled = 1;
}

static int drive_spot_to_spot (void *requester_,
                               responder_capture_t *responder_capture_,
                               reply_capture_t *capture_,
                               int timeout_ms_)
{
    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);

    while (!capture_->completed) {
        pump_requester_progress (requester_);
        if (capture_->completed)
            break;
        if (!responder_capture_->handled)
            sample_pause_ms (1);

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

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    void *requester_node = zlink_spot_node_new (ctx, NULL);
    assert (requester_node != NULL);
    int zero_linger = 0;
    assert (zlink_set_option (requester_node, ZLINK_OPT_LINGER, &zero_linger,
                              sizeof (zero_linger))
            == 0);
    assert (zlink_set_routing_id (requester_node, "sample-requester-node",
                                  strlen ("sample-requester-node"))
            == 0);
    void *requester = zlink_spot_new (requester_node);
    void *responder = zlink_spot_new (requester_node);
    assert (requester != NULL);
    assert (responder != NULL);
    assert (zlink_set_option (requester, ZLINK_OPT_LINGER, &zero_linger,
                              sizeof (zero_linger))
            == 0);
    assert (zlink_set_option (responder, ZLINK_OPT_LINGER, &zero_linger,
                              sizeof (zero_linger))
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

    responder_capture_t responder_capture;
    init_responder_capture (&responder_capture, responder);
    assert (zlink_spot_handler (responder, &handle_spot_request,
                                &responder_capture)
            == ZLINK_HANDLER_OK);

    reply_capture_t capture;
    init_capture (&capture);

    zlink_msg_t request;
    make_message (&request, "spot-ping");
    assert (zlink_spot_request_spot (
              requester, &responder_node_rid, &responder_spot_rid, &request, 1,
              &capture_reply, &capture, ZLINK_SEND_FLAGS_NONE, 5000)
            == ZLINK_SUBMIT_OK);
    assert (drive_spot_to_spot (
      requester, &responder_capture, &capture, 5000));
    if (capture.result != ZLINK_REQUEST_OK) {
        fprintf (stderr, "[spot-routed-request-sample] spot->spot result=%d\n",
                 (int) capture.result);
    }
    assert (capture.result == ZLINK_REQUEST_OK);
    assert (strcmp (capture.reply, "spot-pong") == 0);

    printf ("[spot/routed/request] spot->spot: \"%s\"\n", "spot-pong");
    assert (zlink_spot_destroy (&responder) == ZLINK_CLOSE_OK);
    assert (zlink_spot_destroy (&requester) == ZLINK_CLOSE_OK);
    assert (zlink_spot_node_destroy (&requester_node) == ZLINK_CLOSE_OK);
    assert (zlink_ctx_term (ctx) == ZLINK_CLOSE_OK);
    return 0;
}
