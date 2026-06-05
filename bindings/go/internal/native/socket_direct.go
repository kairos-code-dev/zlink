// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zlink.h"

extern void goZlinkRecvTrampoline(zlink_routing_id_t *source_rid_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_recv_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_recv_handler(s, (zlink_socket_msg_handler_fn)goZlinkRecvTrampoline, (void *)userdata);
}

static inline int zlink_send_ready_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_send_ready_handler(s, (zlink_send_ready_handler_fn)goZlinkSendReadyTrampoline, (void *)userdata);
}

static inline int zlink_router_request_spot_part_go_local(void *router, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_router_request_spot_part(router, dest_node_rid, dest_spot_rid, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}

static inline int zlink_router_send_spot_part_go_local(void *router, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag) {
    return zlink_router_send_spot_part(router, dest_node_rid, dest_spot_rid, part, flags, part_flag);
}

static inline zlink_submit_result_t zlink_direct_send_bytes_go_local(void *s, const void *data, size_t len, zlink_send_flags_t flags) {
    zlink_msg_t part;
    if (zlink_msg_init_size(&part, len) != 0) {
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    if (len > 0 && data != NULL) {
        memcpy(zlink_msg_data(&part), data, len);
    }
    zlink_submit_result_t rc = zlink_send_part(s, &part, flags, ZLINK_PART_FINAL);
    if (rc != ZLINK_SUBMIT_OK) {
        zlink_msg_close(&part);
    }
    return rc;
}

static inline int zlink_direct_recv_bytes_go_local(void *s, void *data, size_t cap, size_t *len, zlink_routing_id_t *source_out, int *has_source, zlink_part_flag_t *has_more, zlink_recv_flags_t flags) {
    const zlink_routing_id_t *source_rid = NULL;
    zlink_msg_t part;
    zlink_part_flag_t more = ZLINK_PART_FINAL;
    if (zlink_msg_init(&part) != 0) {
        return -1;
    }
    int rc = zlink_recv_part(s, &source_rid, &part, &more, flags);
    if (rc != 0) {
        zlink_msg_close(&part);
        return rc;
    }
    size_t size = zlink_msg_size(&part);
    if (size > cap) {
        zlink_msg_close(&part);
        errno = EMSGSIZE;
        return -1;
    }
    if (size > 0 && data != NULL) {
        memcpy(data, zlink_msg_data(&part), size);
    }
    *len = size;
    *has_more = more;
    if (source_rid != NULL) {
        *source_out = *source_rid;
        *has_source = 1;
    } else {
        memset(source_out, 0, sizeof(*source_out));
        *has_source = 0;
    }
    zlink_msg_close(&part);
    return 0;
}
*/
import "C"

import (
	"errors"
	"runtime/cgo"
	"unsafe"
)

type directSocket struct {
	*connectionSocket
}

func (s *directSocket) OnSendReady(handler func()) error {
	return s.setSendReady(handler)
}

func (s *directSocket) submit(flags SendFlags, parts ...*Message) (bool, error) {
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part(s.raw(), part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *directSocket) submitBuilder(flags SendFlags, parts []sendBuilderPart) (bool, error) {
	err := submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part(s.raw(), part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *directSocket) SendBytes(data []byte, flags SendFlags) (bool, error) {
	var dataPtr unsafe.Pointer
	if len(data) > 0 {
		dataPtr = unsafe.Pointer(&data[0])
	}
	err := submitErrorFromResult(C.zlink_direct_send_bytes_go_local(
		s.raw(),
		dataPtr,
		C.size_t(len(data)),
		C.zlink_send_flags_t(flags),
	))
	return submitBackpressureResult(err)
}

func (s *directSocket) RecvBytesInto(buffer []byte, flags RecvFlags) (RecvBytesResult, bool, error) {
	if len(buffer) == 0 {
		return RecvBytesResult{}, false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	var size C.size_t
	var source C.zlink_routing_id_t
	var hasSource C.int
	var hasMore C.zlink_part_flag_t
	err := recvErrorFromResult(C.zlink_direct_recv_bytes_go_local(
		s.raw(),
		unsafe.Pointer(&buffer[0]),
		C.size_t(len(buffer)),
		&size,
		&source,
		&hasSource,
		&hasMore,
		C.zlink_recv_flags_t(flags),
	))
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return RecvBytesResult{}, false, nil
		}
		return RecvBytesResult{}, false, err
	}
	result := RecvBytesResult{
		Size: int(size),
		More: hasMore != C.ZLINK_PART_FINAL,
	}
	if hasSource != 0 {
		result.RoutingID = routingIDFromC(source)
	}
	return result, true, nil
}

func (s *directSocket) Recv(out *Received, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	var sourceRID *C.zlink_routing_id_t
	clonedParts, err := recvMultipart(flags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_recv_part(s.raw(), &sourceRID, part, hasMore, recvFlags))
	})
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	out.replace(routingIDFromCPtr(sourceRID), RoutingID{}, clonedParts, 0, false, nil, nil, nil)
	return true, nil
}

func (s *directSocket) RecvPart(out *Message, flags RecvFlags) (RecvPartResult, bool, error) {
	result, err := recvDirectPartInto(out, flags, func(rid **C.zlink_routing_id_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_recv_part(s.raw(), rid, part, hasMore, recvFlags))
	})
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return RecvPartResult{}, false, nil
		}
		return RecvPartResult{}, false, err
	}
	return result, true, nil
}

func (s *directSocket) onReceive(handler func(*Received)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newRecvCallbackState(recvCallback(handler), nil)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_recv_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.recvHandle != 0 {
		releaseCallbackHandle(s.recvHandle)
	}
	s.recvHandle = handle
	return nil
}
