// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
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
*/
import "C"

import (
	"errors"
	"runtime/cgo"
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
