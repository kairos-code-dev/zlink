#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zlink.h"
#include "api/recv_result_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"

namespace reqrep = zlink::socket_reqrep_internal;

extern "C" int zlink_router_enable_spot_receive(void *router_);
extern "C" int zlink_socket_request_progress_internal(void *socket_);

#ifdef __cplusplus
#define ZLINK_JAVA_EXPORT extern "C"
#define ZLINK_JAVA_GET_ARRAY_CRITICAL(env_, array_) \
    static_cast<jbyte *>((env_)->GetPrimitiveArrayCritical((array_), NULL))
#define ZLINK_JAVA_RELEASE_ARRAY_CRITICAL(env_, array_, ptr_, mode_) \
    (env_)->ReleasePrimitiveArrayCritical((array_), (ptr_), (mode_))
#else
#define ZLINK_JAVA_EXPORT
#define ZLINK_JAVA_GET_ARRAY_CRITICAL(env_, array_) \
    (jbyte *) (*(env_))->GetPrimitiveArrayCritical((env_), (array_), NULL)
#define ZLINK_JAVA_RELEASE_ARRAY_CRITICAL(env_, array_, ptr_, mode_) \
    (*(env_))->ReleasePrimitiveArrayCritical((env_), (array_), (ptr_), \
      (mode_))
#endif

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
                                  void *handle,
                                  int *request_result_out,
                                  zlink_msg_t **reply_parts_out,
                                  size_t *reply_part_count_out) {
    for (;;) {
        pthread_mutex_lock(&state->mutex);
        if (state->done) {
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
            break;
        }
        pthread_mutex_unlock(&state->mutex);

        if (zlink_socket_request_progress_internal(handle) < 0) {
            int request_result = ZLINK_REQUEST_PROTOCOL_ERROR;
            if (errno == ETERM || errno == EFAULT || errno == ENOTSOCK) {
                request_result = ZLINK_REQUEST_TERMINATED;
            }

            pthread_mutex_lock(&state->mutex);
            if (!state->done) {
                state->result = request_result;
                state->reply_parts = NULL;
                state->reply_part_count = 0;
                state->done = 1;
                pthread_cond_signal(&state->cond);
            }
            pthread_mutex_unlock(&state->mutex);
            continue;
        }

        struct timespec sleep_req;
        sleep_req.tv_sec = 0;
        sleep_req.tv_nsec = 1000000L;
        nanosleep(&sleep_req, NULL);
    }
    pthread_cond_destroy(&state->cond);
    pthread_mutex_destroy(&state->mutex);
    return 0;
}

static int zlink_java_validate_router_socket(void *router,
                                             int expected_type) {
    socket_handle_t handle = as_socket_handle(router);
    if (!handle.socket) {
        return -1;
    }
    if (socket_type(handle) != expected_type) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

ZLINK_JAVA_EXPORT int zlink_java_router_recv_compat(
  void *router,
  const zlink_routing_id_t **source_node_rid_out,
  const zlink_routing_id_t **source_spot_rid_out,
  uint64_t *request_seq_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags) {
    if (!source_node_rid_out || !source_spot_rid_out || !request_seq_out
        || !parts_out || !part_count_out) {
        errno = EFAULT;
        return (int) ZLINK_RECV_INVALID_HANDLE;
    }
    if (zlink_java_validate_router_socket(router, ZLINK_CORE_SOCKET_ROUTER)
        != 0) {
        return (int) zlink::recv_result_internal::from_errno(errno);
    }

    socket_handle_t handle = as_socket_handle(router);
    if (!handle.socket) {
        return (int) zlink::recv_result_internal::from_errno(EFAULT);
    }
    if (zlink_router_enable_spot_receive(router) != 0) {
        return (int) zlink::recv_result_internal::from_errno(errno);
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_request_reply_state(handle);

    if (!state) {
        int direct_rc = reqrep::recv_router_message_direct(
          handle, source_node_rid_out, source_spot_rid_out, request_seq_out,
          parts_out, part_count_out, flags);
        if (direct_rc == 0) {
            return (int) ZLINK_RECV_OK;
        }
        return (int) zlink::recv_result_internal::from_errno(errno);
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    bool has_recv_queue = state->recv_queue.rx || state->recv_queue.tx;
    bool can_drain_direct =
      !has_recv_queue && !state->internal_dispatch_installed
      && state->pending_requests.empty() && state->pending_sequences.empty();
    lock.unlock();

    if (can_drain_direct) {
        int direct_rc = reqrep::recv_router_message_direct(
          handle, source_node_rid_out, source_spot_rid_out, request_seq_out,
          parts_out, part_count_out, flags);
        if (direct_rc == 0) {
            return (int) ZLINK_RECV_OK;
        }
        return (int) zlink::recv_result_internal::from_errno(errno);
    }

    if (reqrep::ensure_recv_queue_ready(state) != 0
        || reqrep::ensure_internal_dispatch_installed(state) != 0) {
        return (int) zlink::recv_result_internal::from_errno(errno);
    }

    int timeout_ms = -1;
    size_t timeout_size = sizeof(timeout_ms);
    if (handle.socket->getsockopt(ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout_ms,
                                  &timeout_size)
        != 0) {
        return (int) zlink::recv_result_internal::from_errno(errno);
    }

    return (int) zlink::recv_result_internal::from_rc(
      reqrep::recv_internal_router_queue(
        &state->recv_queue, source_node_rid_out, source_spot_rid_out,
        request_seq_out, parts_out, part_count_out, flags, timeout_ms));
}

ZLINK_JAVA_EXPORT int zlink_java_dealer_request_sync(void *dealer,
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

    return zlink_java_reqrep_wait(&state, dealer, request_result_out,
      reply_parts_out, reply_part_count_out);
}

ZLINK_JAVA_EXPORT int zlink_java_router_request_sync(
  void *router,
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

    return zlink_java_reqrep_wait(&state, router, request_result_out,
      reply_parts_out, reply_part_count_out);
}

ZLINK_JAVA_EXPORT uintptr_t zlink_java_msg_data_addr(zlink_msg_t *msg) {
    return (uintptr_t) zlink_msg_data(msg);
}

ZLINK_JAVA_EXPORT int zlink_java_send_u32(void *socket,
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

ZLINK_JAVA_EXPORT uint64_t zlink_java_bench_noop_u64(uint64_t value) {
    return value + 1u;
}

ZLINK_JAVA_EXPORT void zlink_java_bench_copy_to_native(const uint8_t *src,
                                                       uint8_t *dst,
                                                       size_t len) {
    if (len == 0) {
        return;
    }
    memcpy(dst, src, len);
}

ZLINK_JAVA_EXPORT void zlink_java_bench_copy_to_heap(const uint8_t *src,
                                                     uint8_t *dst,
                                                     size_t len) {
    if (len == 0) {
        return;
    }
    memcpy(dst, src, len);
}

ZLINK_JAVA_EXPORT JNIEXPORT jlong JNICALL
Java_dev_kairoscode_zlink_FfmVsJniMicrobench_jniNoop0(
  JNIEnv *env,
  jclass cls,
  jlong value) {
    (void) env;
    (void) cls;
    return (jlong) zlink_java_bench_noop_u64((uint64_t) value);
}

ZLINK_JAVA_EXPORT JNIEXPORT void JNICALL
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
    src_ptr = ZLINK_JAVA_GET_ARRAY_CRITICAL(env, src);
    if (src_ptr == NULL) {
        return;
    }
    zlink_java_bench_copy_to_native(
      (const uint8_t *) (src_ptr + offset),
      (uint8_t *) (uintptr_t) dst_address,
      (size_t) length);
    ZLINK_JAVA_RELEASE_ARRAY_CRITICAL(env, src, src_ptr, JNI_ABORT);
}

ZLINK_JAVA_EXPORT JNIEXPORT void JNICALL
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
    dst_ptr = ZLINK_JAVA_GET_ARRAY_CRITICAL(env, dst);
    if (dst_ptr == NULL) {
        return;
    }
    zlink_java_bench_copy_to_heap(
      (const uint8_t *) (uintptr_t) src_address,
      (uint8_t *) (dst_ptr + offset),
      (size_t) length);
    ZLINK_JAVA_RELEASE_ARRAY_CRITICAL(env, dst, dst_ptr, 0);
}

ZLINK_JAVA_EXPORT JNIEXPORT void JNICALL
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

ZLINK_JAVA_EXPORT JNIEXPORT void JNICALL
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

ZLINK_JAVA_EXPORT JNIEXPORT void JNICALL
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

ZLINK_JAVA_EXPORT JNIEXPORT void JNICALL
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
