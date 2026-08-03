// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zlink.h"

extern void goZlinkRecvTrampoline(zlink_routing_id_t *source_rid_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_recv_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_recv_handler(s, (zlink_socket_msg_handler_fn)goZlinkRecvTrampoline, (void *)userdata);
}
*/
import "C"

import "runtime/cgo"

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
		return false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	var sourceRID *C.zlink_routing_id_t
	clonedParts, err := recvMultipart(flags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_recv_part(s.raw(), &sourceRID, part, hasMore, recvFlags))
	})
	if err != nil {
		if isNoData(err) {
			return false, nil
		}
		return false, err
	}
	out.replace(routingIDFromCPtr(sourceRID), clonedParts, 0, false, nil, nil, nil)
	return true, nil
}

func (s *directSocket) RecvPart(out *Message, flags RecvFlags) (RecvPartResult, bool, error) {
	result, err := recvDirectPartInto(out, flags, func(rid **C.zlink_routing_id_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_recv_part(s.raw(), rid, part, hasMore, recvFlags))
	})
	if err != nil {
		if isNoData(err) {
			return RecvPartResult{}, false, nil
		}
		return RecvPartResult{}, false, err
	}
	return result, true, nil
}

func (s *directSocket) onReceive(handler func(*Received)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	state := newRecvCallbackState(recvCallback(handler))
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
