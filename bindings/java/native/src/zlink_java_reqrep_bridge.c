#include <jni.h>
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

    if (parts == NULL || part_count == 0) {
        pthread_cond_destroy(&state.cond);
        pthread_mutex_destroy(&state.mutex);
        return (int) ZLINK_SUBMIT_INVALID_STATE;
    }

    for (size_t i = 0; i < part_count; ++i) {
        submit_result = zlink_dealer_request_part(
          dealer, &parts[i], (zlink_send_flags_t) flags,
          i + 1u < part_count ? ZLINK_PART_MORE : ZLINK_PART_FINAL,
          i + 1u < part_count ? 0u : timeout_ms,
          i + 1u < part_count ? NULL : zlink_java_reqrep_complete,
          i + 1u < part_count ? NULL : &state);
        if (submit_result != ZLINK_SUBMIT_OK) {
            pthread_cond_destroy(&state.cond);
            pthread_mutex_destroy(&state.mutex);
            return (int) submit_result;
        }
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

    if (parts == NULL || part_count == 0) {
        pthread_cond_destroy(&state.cond);
        pthread_mutex_destroy(&state.mutex);
        return (int) ZLINK_SUBMIT_INVALID_STATE;
    }

    for (size_t i = 0; i < part_count; ++i) {
        submit_result = zlink_router_request_part(
          router, peer_rid, &parts[i], (zlink_send_flags_t) flags,
          i + 1u < part_count ? ZLINK_PART_MORE : ZLINK_PART_FINAL,
          i + 1u < part_count ? 0u : timeout_ms,
          i + 1u < part_count ? NULL : zlink_java_reqrep_complete,
          i + 1u < part_count ? NULL : &state);
        if (submit_result != ZLINK_SUBMIT_OK) {
            pthread_cond_destroy(&state.cond);
            pthread_mutex_destroy(&state.mutex);
            return (int) submit_result;
        }
    }

    return zlink_java_reqrep_wait(&state, request_result_out,
      reply_parts_out, reply_part_count_out);
}

uintptr_t zlink_java_msg_data_addr(zlink_msg_t *msg) {
    return (uintptr_t) zlink_msg_data(msg);
}

int zlink_java_send_u32(void *socket,
                        uint32_t routing_id,
                        zlink_msg_t *parts,
                        size_t part_count,
                        int flags) {
    zlink_routing_id_t rid;
    rid.size = 4;
    rid.data[0] = (uint8_t) (routing_id >> 24);
    rid.data[1] = (uint8_t) (routing_id >> 16);
    rid.data[2] = (uint8_t) (routing_id >> 8);
    rid.data[3] = (uint8_t) routing_id;
    if (parts == NULL || part_count == 0) {
        return (int) ZLINK_SUBMIT_INVALID_STATE;
    }

    for (size_t i = 0; i < part_count; ++i) {
        int rc = zlink_send_part_rid(socket, &rid, &parts[i],
          (zlink_send_flags_t) flags,
          i + 1u < part_count ? ZLINK_PART_MORE : ZLINK_PART_FINAL);
        if (rc != ZLINK_SUBMIT_OK) {
            return rc;
        }
    }
    return (int) ZLINK_SUBMIT_OK;
}

uint64_t zlink_java_bench_noop_u64(uint64_t value) {
    return value + 1u;
}

void zlink_java_bench_copy_to_native(const uint8_t *src,
                                     uint8_t *dst,
                                     size_t len) {
    if (len == 0) {
        return;
    }
    memcpy(dst, src, len);
}

void zlink_java_bench_copy_to_heap(const uint8_t *src,
                                   uint8_t *dst,
                                   size_t len) {
    if (len == 0) {
        return;
    }
    memcpy(dst, src, len);
}

JNIEXPORT jlong JNICALL
Java_dev_kairoscode_zlink_FfmVsJniMicrobench_jniNoop0(
  JNIEnv *env,
  jclass cls,
  jlong value) {
    (void) env;
    (void) cls;
    return (jlong) zlink_java_bench_noop_u64((uint64_t) value);
}

JNIEXPORT void JNICALL
Java_dev_kairoscode_zlink_FfmVsJniMicrobench_jniCopyToNative0(
  JNIEnv *env,
  jclass cls,
  jbyteArray src,
  jint offset,
  jlong dst_address,
  jint length) {
    jbyte *src_ptr;

    (void) cls;
    if (src == NULL || length < 0 || offset < 0) {
        return;
    }
    src_ptr = (*env)->GetPrimitiveArrayCritical(env, src, NULL);
    if (src_ptr == NULL) {
        return;
    }
    zlink_java_bench_copy_to_native(
      (const uint8_t *) (src_ptr + offset),
      (uint8_t *) (uintptr_t) dst_address,
      (size_t) length);
    (*env)->ReleasePrimitiveArrayCritical(env, src, src_ptr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_dev_kairoscode_zlink_FfmVsJniMicrobench_jniCopyToHeap0(
  JNIEnv *env,
  jclass cls,
  jlong src_address,
  jbyteArray dst,
  jint offset,
  jint length) {
    jbyte *dst_ptr;

    (void) cls;
    if (dst == NULL || length < 0 || offset < 0) {
        return;
    }
    dst_ptr = (*env)->GetPrimitiveArrayCritical(env, dst, NULL);
    if (dst_ptr == NULL) {
        return;
    }
    zlink_java_bench_copy_to_heap(
      (const uint8_t *) (uintptr_t) src_address,
      (uint8_t *) (dst_ptr + offset),
      (size_t) length);
    (*env)->ReleasePrimitiveArrayCritical(env, dst, dst_ptr, 0);
}

JNIEXPORT void JNICALL
Java_dev_kairoscode_zlink_MsgInteropFfmVsJniMicrobench_jniMsgInitClose0(
  JNIEnv *env,
  jclass cls,
  jlong msg_address) {
    zlink_msg_t *msg;

    (void) env;
    (void) cls;
    msg = (zlink_msg_t *) (uintptr_t) msg_address;
    if (zlink_msg_init(msg) != 0) {
        return;
    }
    zlink_msg_close(msg);
}

JNIEXPORT void JNICALL
Java_dev_kairoscode_zlink_MsgInteropFfmVsJniMicrobench_jniMsgInitSizeClose0(
  JNIEnv *env,
  jclass cls,
  jlong msg_address,
  jint size) {
    zlink_msg_t *msg;

    (void) env;
    (void) cls;
    msg = (zlink_msg_t *) (uintptr_t) msg_address;
    if (zlink_msg_init_size(msg, (size_t) size) != 0) {
        return;
    }
    zlink_msg_close(msg);
}

JNIEXPORT void JNICALL
Java_dev_kairoscode_zlink_MsgInteropFfmVsJniMicrobench_jniMsgMovePath0(
  JNIEnv *env,
  jclass cls,
  jlong src_address,
  jlong dst_address,
  jint size) {
    zlink_msg_t *src;
    zlink_msg_t *dst;

    (void) env;
    (void) cls;
    src = (zlink_msg_t *) (uintptr_t) src_address;
    dst = (zlink_msg_t *) (uintptr_t) dst_address;
    if (zlink_msg_init_size(src, (size_t) size) != 0) {
        return;
    }
    if (zlink_msg_init(dst) != 0) {
        zlink_msg_close(src);
        return;
    }
    if (zlink_msg_move(dst, src) != 0) {
        zlink_msg_close(src);
        zlink_msg_close(dst);
        return;
    }
    zlink_msg_close(dst);
}

JNIEXPORT void JNICALL
Java_dev_kairoscode_zlink_MsgInteropFfmVsJniMicrobench_jniMsgCopyPath0(
  JNIEnv *env,
  jclass cls,
  jlong src_address,
  jlong dst_address,
  jint size) {
    zlink_msg_t *src;
    zlink_msg_t *dst;

    (void) env;
    (void) cls;
    src = (zlink_msg_t *) (uintptr_t) src_address;
    dst = (zlink_msg_t *) (uintptr_t) dst_address;
    if (zlink_msg_init_size(src, (size_t) size) != 0) {
        return;
    }
    if (zlink_msg_init(dst) != 0) {
        zlink_msg_close(src);
        return;
    }
    if (zlink_msg_copy(dst, src) != 0) {
        zlink_msg_close(src);
        zlink_msg_close(dst);
        return;
    }
    zlink_msg_close(src);
    zlink_msg_close(dst);
}
