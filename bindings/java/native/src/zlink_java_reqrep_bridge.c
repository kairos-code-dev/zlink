#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zlink.h"

typedef struct zlink_java_reqrep_state_t {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int done;
    int result;
    zlink_msg_t *reply_parts;
    size_t reply_part_count;
} zlink_java_reqrep_state_t;

static int zlink_java_reqrep_copy_parts(zlink_msg_t *parts,
                                        size_t part_count,
                                        zlink_msg_t **out_parts) {
    size_t i;
    zlink_msg_t *copy;

    *out_parts = NULL;
    if (parts == NULL || part_count == 0) {
        return 0;
    }

    copy = (zlink_msg_t *) calloc(part_count, sizeof(zlink_msg_t));
    if (copy == NULL) {
        return -1;
    }

    for (i = 0; i < part_count; ++i) {
        if (zlink_msg_init(&copy[i]) != 0) {
            goto fail;
        }
        if (zlink_msg_copy(&copy[i], &parts[i]) != 0) {
            zlink_msg_close(&copy[i]);
            goto fail;
        }
    }

    *out_parts = copy;
    return 0;

fail:
    while (i > 0) {
        --i;
        zlink_msg_close(&copy[i]);
    }
    free(copy);
    return -1;
}

static void zlink_java_reqrep_complete(zlink_request_result_t result,
                                       zlink_msg_t *parts,
                                       size_t part_count,
                                       void *userdata) {
    zlink_java_reqrep_state_t *state =
      (zlink_java_reqrep_state_t *) userdata;

    pthread_mutex_lock(&state->mutex);
    state->result = (int) result;
    state->reply_parts = NULL;
    state->reply_part_count = 0;

    if (result == ZLINK_REQUEST_OK
        && zlink_java_reqrep_copy_parts(parts, part_count,
              &state->reply_parts) == 0) {
        state->reply_part_count = part_count;
    } else if (result == ZLINK_REQUEST_OK) {
        state->result = ZLINK_REQUEST_INTERNAL_ERROR;
    }

    zlink_multipart_close(parts, part_count);
    state->done = 1;
    pthread_cond_signal(&state->cond);
    pthread_mutex_unlock(&state->mutex);
}

static int zlink_java_reqrep_wait(zlink_java_reqrep_state_t *state,
                                  int *request_result_out,
                                  zlink_msg_t **reply_parts_out,
                                  size_t *reply_part_count_out) {
    pthread_mutex_lock(&state->mutex);
    while (!state->done) {
        pthread_cond_wait(&state->cond, &state->mutex);
    }

    if (request_result_out != NULL) {
        *request_result_out = state->result;
    }
    if (reply_parts_out != NULL) {
        *reply_parts_out = state->reply_parts;
    }
    if (reply_part_count_out != NULL) {
        *reply_part_count_out = state->reply_part_count;
    }

    pthread_mutex_unlock(&state->mutex);
    pthread_cond_destroy(&state->cond);
    pthread_mutex_destroy(&state->mutex);
    return 0;
}

int zlink_java_dealer_request_sync(void *dealer,
                                   zlink_msg_t *parts,
                                   size_t part_count,
                                   int flags,
                                   uint32_t timeout_ms,
                                   int *request_result_out,
                                   zlink_msg_t **reply_parts_out,
                                   size_t *reply_part_count_out) {
    zlink_java_reqrep_state_t state;
    zlink_submit_result_t submit_result;

    memset(&state, 0, sizeof(state));
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.cond, NULL);

    submit_result = zlink_dealer_request(dealer, parts, part_count,
      zlink_java_reqrep_complete, &state, (zlink_send_flags_t) flags, timeout_ms);
    if (submit_result != ZLINK_SUBMIT_OK) {
        pthread_cond_destroy(&state.cond);
        pthread_mutex_destroy(&state.mutex);
        return (int) submit_result;
    }

    return zlink_java_reqrep_wait(&state, request_result_out,
      reply_parts_out, reply_part_count_out);
}

int zlink_java_router_request_sync(void *router,
                                   const zlink_routing_id_t *peer_rid,
                                   zlink_msg_t *parts,
                                   size_t part_count,
                                   int flags,
                                   uint32_t timeout_ms,
                                   int *request_result_out,
                                   zlink_msg_t **reply_parts_out,
                                   size_t *reply_part_count_out) {
    zlink_java_reqrep_state_t state;
    zlink_submit_result_t submit_result;

    memset(&state, 0, sizeof(state));
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.cond, NULL);

    submit_result = zlink_router_request(router, peer_rid, parts, part_count,
      zlink_java_reqrep_complete, &state, (zlink_send_flags_t) flags, timeout_ms);
    if (submit_result != ZLINK_SUBMIT_OK) {
        pthread_cond_destroy(&state.cond);
        pthread_mutex_destroy(&state.mutex);
        return (int) submit_result;
    }

    return zlink_java_reqrep_wait(&state, request_result_out,
      reply_parts_out, reply_part_count_out);
}
