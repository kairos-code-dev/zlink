/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_C_SAMPLES_ACTOR_SAMPLE_COMMON_H_INCLUDED
#define ZLINK_C_SAMPLES_ACTOR_SAMPLE_COMMON_H_INCLUDED

#include "sample_common.h"

typedef struct
{
    void *node;
    callback_signal_t join_signal;
    callback_signal_t actor_signal;
    zlink_request_result_t join_result;
    zlink_actor_ref_t joined_actor;
    int accept_join;
    char payload[256];
    size_t payload_len;
} actor_sample_capture_t;

static inline void actor_sample_capture_init (actor_sample_capture_t *capture)
{
    callback_signal_init (&capture->join_signal);
    callback_signal_init (&capture->actor_signal);
    capture->node = NULL;
    capture->join_result = ZLINK_REQUEST_INTERNAL_ERROR;
    memset (&capture->joined_actor, 0, sizeof (capture->joined_actor));
    capture->accept_join = 1;
    capture->payload_len = 0;
    memset (capture->payload, 0, sizeof (capture->payload));
}

static inline void actor_sample_capture_destroy (
  actor_sample_capture_t *capture)
{
    callback_signal_destroy (&capture->actor_signal);
    callback_signal_destroy (&capture->join_signal);
}

static inline void actor_sample_capture_reset (actor_sample_capture_t *capture)
{
    pthread_mutex_lock (&capture->join_signal.mutex);
    capture->join_signal.ready = 0;
    pthread_mutex_unlock (&capture->join_signal.mutex);
    capture->join_result = ZLINK_REQUEST_INTERNAL_ERROR;
}

static inline void actor_sample_set_rid (zlink_routing_id_t *rid,
                                         const char *text)
{
    size_t len = strlen (text);
    assert (len > 0 && len <= sizeof (rid->data));
    memset (rid, 0, sizeof (*rid));
    rid->size = (uint8_t) len;
    memcpy (rid->data, text, len);
}

static inline int actor_sample_rid_equals (const zlink_routing_id_t *lhs,
                                           const zlink_routing_id_t *rhs)
{
    return lhs->size == rhs->size
           && memcmp (lhs->data, rhs->data, lhs->size) == 0;
}

static inline zlink_routing_id_t actor_sample_find_spot_rid_not (
  void *node, const zlink_routing_id_t *excluded, size_t excluded_count)
{
    size_t count = 0;
    assert (zlink_spot_node_spots_snapshot (node, NULL, &count)
            == ZLINK_CONFIG_OK);
    zlink_spot_node_spot_entry_t *rows =
      (zlink_spot_node_spot_entry_t *) calloc (count, sizeof (*rows));
    assert (rows != NULL);
    assert (zlink_spot_node_spots_snapshot (node, rows, &count)
            == ZLINK_CONFIG_OK);
    for (size_t i = 0; i < count; ++i) {
        int skip = 0;
        for (size_t j = 0; j < excluded_count; ++j) {
            if (actor_sample_rid_equals (&rows[i].spot_rid, &excluded[j])) {
                skip = 1;
                break;
            }
        }
        if (!skip) {
            zlink_routing_id_t rid = rows[i].spot_rid;
            free (rows);
            return rid;
        }
    }
    free (rows);
    assert (!"Spot rid not found");
    zlink_routing_id_t empty;
    memset (&empty, 0, sizeof (empty));
    return empty;
}

static void actor_sample_reply (zlink_request_result_t result,
                                zlink_msg_t *parts,
                                size_t part_count,
                                void *userdata)
{
    actor_sample_capture_t *capture = (actor_sample_capture_t *) userdata;
    assert (capture != NULL);
    capture->join_result = result;
    if (parts != NULL && part_count > 0)
        zlink_multipart_close (parts, part_count);
    callback_signal_set (&capture->join_signal);
}

static void actor_sample_join_reply (const zlink_actor_join_result_t *result,
                                     zlink_msg_t *parts,
                                     size_t part_count,
                                     void *userdata)
{
    assert (result != NULL);
    actor_sample_capture_t *capture = (actor_sample_capture_t *) userdata;
    if (capture != NULL && result->result == ZLINK_REQUEST_OK)
        capture->joined_actor = result->actor;
    actor_sample_reply (result->result, parts, part_count, userdata);
}

static void actor_sample_dispatch (void *spot,
                                   const zlink_spot_dispatch_info_t *info,
                                   void *userdata)
{
    actor_sample_capture_t *capture = (actor_sample_capture_t *) userdata;
    assert (capture != NULL);
    if (info->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE) {
        zlink_actor_join_info_t join_info;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        assert (zlink_spot_actor_join_recv (
                  spot, &join_info, &parts, &part_count, ZLINK_DONTWAIT)
                == ZLINK_RECV_OK);
        zlink_multipart_close (parts, part_count);
        assert (zlink_spot_actor_join_reply (
                  spot, &join_info, capture->accept_join ? 1u : 0u, NULL, 0)
                == ZLINK_SUBMIT_OK);
        return;
    }

    if (info->event != ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE)
        return;
    assert (spot != NULL);
    assert (info->subject_kind == ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR);
    for (;;) {
        zlink_actor_recv_info_t recv_info;
        zlink_msg_t part;
        zlink_part_flag_t flag = ZLINK_PART_FINAL;
        zlink_recv_result_t rc = zlink_spot_node_actor_recv_part (
          capture->node, (const zlink_actor_ref_t *) info->subject, &recv_info,
          &part, &flag, ZLINK_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA)
            break;
        assert (rc == ZLINK_RECV_OK);
        size_t part_size = zlink_msg_size (&part);
        assert (capture->payload_len + part_size < sizeof (capture->payload));
        memcpy (capture->payload + capture->payload_len, zlink_msg_data (&part),
                part_size);
        capture->payload_len += part_size;
        capture->payload[capture->payload_len] = '\0';
        zlink_msg_close (&part);
    }
    callback_signal_set (&capture->actor_signal);
}

static void actor_sample_join_only_dispatch (
  void *spot, const zlink_spot_dispatch_info_t *info, void *userdata)
{
    actor_sample_capture_t *capture = (actor_sample_capture_t *) userdata;
    assert (capture != NULL);
    if (info->event != ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE)
        return;
    zlink_actor_join_info_t join_info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    assert (zlink_spot_actor_join_recv (info->subject, &join_info, &parts,
                                        &part_count, ZLINK_DONTWAIT)
            == ZLINK_RECV_OK);
    zlink_multipart_close (parts, part_count);
    assert (zlink_spot_actor_join_reply (spot, &join_info,
                                         capture->accept_join ? 1u : 0u,
                                         NULL, 0)
            == ZLINK_SUBMIT_OK);
}

#endif
