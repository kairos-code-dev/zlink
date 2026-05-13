#include <jni.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "zlink.h"

extern "C" int zlink_router_enable_spot_receive(void *router_);

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

static zlink_recv_result_t zlink_java_recv_result_from_errno(int err) {
    switch (err) {
    case EAGAIN:
        return ZLINK_RECV_NO_DATA;
    case EBUSY:
        return ZLINK_RECV_BUSY;
    case EFAULT:
        return ZLINK_RECV_INVALID_HANDLE;
    case ENOTSUP:
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP:
#endif
        return ZLINK_RECV_NOT_SUPPORTED;
    default:
        return ZLINK_RECV_INTERNAL_ERROR;
    }
}

static void zlink_java_close_router_recv_parts(std::vector<zlink_msg_t> &parts) {
    for (size_t i = 0; i < parts.size(); ++i) {
        zlink_msg_close(&parts[i]);
    }
    parts.clear();
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
    if (router == NULL) {
        errno = EFAULT;
        return (int) ZLINK_RECV_INVALID_HANDLE;
    }
    if (zlink_router_enable_spot_receive(router) != 0) {
        return (int) zlink_java_recv_result_from_errno(errno);
    }

    static thread_local std::vector<zlink_msg_t> router_recv_parts;
    zlink_java_close_router_recv_parts(router_recv_parts);
    *source_node_rid_out = NULL;
    *source_spot_rid_out = NULL;
    *request_seq_out = 0;
    *parts_out = NULL;
    *part_count_out = 0;
    zlink_recv_flags_t recv_flags = flags;

    while (true) {
        router_recv_parts.push_back(zlink_msg_t());
        zlink_msg_t &part = router_recv_parts.back();
        if (zlink_msg_init(&part) != 0) {
            router_recv_parts.pop_back();
            zlink_java_close_router_recv_parts(router_recv_parts);
            return (int) zlink_java_recv_result_from_errno(errno);
        }

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_recv_result_t rc = zlink_router_recv_part(
          router, source_node_rid_out, source_spot_rid_out, request_seq_out,
          &part, &has_more, recv_flags);
        if (rc != ZLINK_RECV_OK) {
            zlink_msg_close(&part);
            router_recv_parts.pop_back();
            zlink_java_close_router_recv_parts(router_recv_parts);
            return (int) rc;
        }
        if (has_more == ZLINK_PART_FINAL) {
            *parts_out = router_recv_parts.data();
            *part_count_out = router_recv_parts.size();
            return (int) ZLINK_RECV_OK;
        }
        recv_flags = (zlink_recv_flags_t) ZLINK_DONTWAIT;
    }
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
