#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
Java_systems_zlink_FfmVsJniMicrobench_jniNoop0(
  JNIEnv *env,
  jclass cls,
  jlong value) {
    (void) env;
    (void) cls;
    return (jlong) zlink_java_bench_noop_u64((uint64_t) value);
}

ZLINK_JAVA_EXPORT JNIEXPORT void JNICALL
Java_systems_zlink_FfmVsJniMicrobench_jniCopyToNative0(
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
Java_systems_zlink_FfmVsJniMicrobench_jniCopyToHeap0(
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
Java_systems_zlink_MsgInteropFfmVsJniMicrobench_jniMsgInitClose0(
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
Java_systems_zlink_MsgInteropFfmVsJniMicrobench_jniMsgInitSizeClose0(
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
Java_systems_zlink_MsgInteropFfmVsJniMicrobench_jniMsgMovePath0(
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
Java_systems_zlink_MsgInteropFfmVsJniMicrobench_jniMsgCopyPath0(
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
