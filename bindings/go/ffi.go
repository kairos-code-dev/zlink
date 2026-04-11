// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#cgo CFLAGS: -I${SRCDIR}/include
#cgo linux,amd64 LDFLAGS: -L${SRCDIR}/native/linux-x86_64 -lzlink -Wl,-rpath,${SRCDIR}/native/linux-x86_64
#cgo linux,arm64 LDFLAGS: -L${SRCDIR}/native/linux-aarch64 -lzlink -Wl,-rpath,${SRCDIR}/native/linux-aarch64
#cgo darwin,amd64 LDFLAGS: -L${SRCDIR}/native/darwin-x86_64 -lzlink -Wl,-rpath,${SRCDIR}/native/darwin-x86_64
#cgo darwin,arm64 LDFLAGS: -L${SRCDIR}/native/darwin-aarch64 -lzlink -Wl,-rpath,${SRCDIR}/native/darwin-aarch64
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zlink.h"

extern void goZlinkRecvTrampoline(zlink_routing_id_t *source_rid_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSubscribeTrampoline(zlink_routing_id_t *source_rid_, char *topic_, size_t topic_len_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);
extern void goZlinkMonitorTrampoline(zlink_monitor_event_t *event_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(int errno_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkRouterRequestTrampoline(const zlink_routing_id_t *peer_rid_, uint64_t request_seq_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_bind_go(void *s, const char *addr) {
    return zlink_bind(s, addr);
}

static inline int zlink_connect_go(void *s, const char *addr) {
    return zlink_connect(s, addr);
}

static inline int zlink_unbind_go(void *s, const char *addr) {
    return zlink_unbind(s, addr);
}

static inline int zlink_disconnect_go(void *s, const char *addr) {
    return zlink_disconnect(s, addr);
}

static inline int zlink_recv_handler_go(void *s, uintptr_t userdata) {
    return zlink_recv_handler(s, (zlink_socket_msg_handler_fn)goZlinkRecvTrampoline, (void *)userdata);
}

static inline int zlink_subscribe_handler_go(void *s, uintptr_t userdata) {
    return zlink_subscribe_handler(s, (zlink_subscribe_handler_fn)goZlinkSubscribeTrampoline, (void *)userdata);
}

static inline int zlink_send_ready_handler_go(void *s, uintptr_t userdata) {
    return zlink_send_ready_handler(s, (zlink_send_ready_handler_fn)goZlinkSendReadyTrampoline, (void *)userdata);
}

static inline int zlink_socket_monitor_handler_go(void *m, uintptr_t userdata) {
    return zlink_socket_monitor_handler(m, (zlink_socket_monitor_handler_fn)goZlinkMonitorTrampoline, (void *)userdata);
}

static inline int zlink_dealer_request_go(void *dealer, zlink_msg_t *parts, size_t part_count, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_dealer_request(dealer, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, 0, timeout_ms);
}

static inline int zlink_router_request_go(void *router, const zlink_routing_id_t *peer_rid, zlink_msg_t *parts, size_t part_count, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_router_request(router, peer_rid, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, 0, timeout_ms);
}

static inline int zlink_router_handler_go(void *router, uintptr_t userdata) {
    return zlink_router_handler(router, (zlink_router_handler_fn)goZlinkRouterRequestTrampoline, (void *)userdata);
}
*/
import "C"
