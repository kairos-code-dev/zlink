#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

#include "zlink.h"

extern "C" int zlink_router_enable_spot_receive(void *router_);

#ifdef __cplusplus
#define ZLINK_JAVA_EXPORT extern "C"
#else
#define ZLINK_JAVA_EXPORT
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

ZLINK_JAVA_EXPORT int zlink_java_router_recv(
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
