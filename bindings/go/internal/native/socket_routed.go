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
	"time"
)

type routedSocket struct {
	*connectionSocket
}

func (s *routedSocket) OnSendReady(handler func()) error {
	return s.setSendReady(handler)
}

func (s *routedSocket) submitTo(target RoutingID, flags SendFlags, parts ...*Message) (bool, error) {
	rid := target.toC()
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part_rid(s.raw(), &rid, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) submitToBuilder(target RoutingID, flags SendFlags, parts []sendBuilderPart) (bool, error) {
	rid := target.toC()
	err := submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part_rid(s.raw(), &rid, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) submitToSpot(destNodeRid, destSpotRid RoutingID, flags SendFlags, parts ...*Message) (bool, error) {
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_send_spot_part_go_local(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) submitToSpotBuilder(destNodeRid, destSpotRid RoutingID, flags SendFlags, parts []sendBuilderPart) (bool, error) {
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	err := submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_send_spot_part_go_local(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) requestToSpot(destNodeRid, destSpotRid RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	state, err := s.startSpotRequest(destNodeRid, destSpotRid, flags, timeout, parts...)
	ok, err := submitBackpressureResult(err)
	if err != nil {
		return false, err
	}
	if !ok {
		return false, nil
	}
	dispatchRequestCallback(state, callback)
	return true, nil
}

func (s *routedSocket) requestToSpotCallback(destNodeRid, destSpotRid RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	return s.requestToSpot(destNodeRid, destSpotRid, callback, flags, timeout, parts...)
}

func (s *routedSocket) reply(rid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
	if err := validateReplyFlags(flags); err != nil {
		return err
	}
	target := rid.toC()
	return submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_reply_part(s.raw(), &target, C.uint64_t(requestSeq), part, partFlag))
	})
}

func (s *routedSocket) replyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) (bool, error) {
	if err := validateReplyFlags(flags); err != nil {
		return false, err
	}
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	err := submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_reply_spot_part(s.raw(), &node, &spot, C.uint64_t(requestSeq), part, partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) recvInto(out *Received, flags RecvFlags) error {
	recvOnce := func(recvFlags RecvFlags) (*C.zlink_routing_id_t, *C.zlink_routing_id_t, C.uint64_t, []*Message, error) {
		var nodeRID *C.zlink_routing_id_t
		var spotRID *C.zlink_routing_id_t
		var requestSeq C.uint64_t
		parts, err := recvMultipart(recvFlags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, currentFlags C.zlink_recv_flags_t) error {
			return recvErrorFromResult(C.zlink_router_recv_part(s.raw(), &nodeRID, &spotRID, &requestSeq, part, hasMore, currentFlags))
		})
		if err != nil {
			return nil, nil, 0, nil, err
		}
		return nodeRID, spotRID, requestSeq, parts, nil
	}

	var nodeRID *C.zlink_routing_id_t
	var spotRID *C.zlink_routing_id_t
	var requestSeq C.uint64_t
	var parts []*Message
	if flags == RecvFlagsNone {
		primedNodeRID, primedSpotRID, primedRequestSeq, primedParts, primedErr := recvOnce(RecvFlagsDontWait)
		if primedErr == nil {
			nodeRID = primedNodeRID
			spotRID = primedSpotRID
			requestSeq = primedRequestSeq
			parts = primedParts
		} else {
			var recvErr *RecvError
			if !errors.As(primedErr, &recvErr) || recvErr.Result != RecvNoData {
				return primedErr
			}
			var err error
			nodeRID, spotRID, requestSeq, parts, err = recvOnce(flags)
			if err != nil {
				return err
			}
		}
	} else {
		var err error
		nodeRID, spotRID, requestSeq, parts, err = recvOnce(flags)
		if err != nil {
			return err
		}
	}
	routingID := routingIDFromCPtr(nodeRID)
	spotRoutingID := routingIDFromCPtr(spotRID)
	seq := uint64(requestSeq)
	hasSeq := requestSeq != 0
	var reply func(SendFlags, []*Message) error
	var send func(SendFlags, []*Message) (bool, error)
	var sendBuilder func(SendFlags, []sendBuilderPart) (bool, error)
	if hasSeq {
		if spotRoutingID.Size() == 0 {
			reply = receivedReplyToRouter(s.reply, routingID, seq)
		} else {
			reply = func(flags SendFlags, parts []*Message) error {
				_, err := s.replyToSpot(routingID, spotRoutingID, seq, flags, parts...)
				return err
			}
		}
	}
	if routingID.Size() > 0 {
		if spotRoutingID.Size() == 0 {
			send = receivedSendToRouter(s.submitTo, routingID)
			sendBuilder = func(flags SendFlags, parts []sendBuilderPart) (bool, error) {
				return s.submitToBuilder(routingID, flags, parts)
			}
		} else {
			send = func(flags SendFlags, parts []*Message) (bool, error) {
				return s.submitToSpot(routingID, spotRoutingID, flags, parts...)
			}
			sendBuilder = func(flags SendFlags, parts []sendBuilderPart) (bool, error) {
				return s.submitToSpotBuilder(routingID, spotRoutingID, flags, parts)
			}
		}
	}
	out.replace(routingID, spotRoutingID, parts, seq, hasSeq, reply, send, sendBuilder)
	return nil
}

func (s *routedSocket) Recv(out *Received, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	if s.recvHandle != 0 {
		return false, &RecvError{Result: RecvBusy, nativeErrno: int(C.EBUSY)}
	}
	if err := s.recvInto(out, flags); err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

func (s *routedSocket) RecvPart(out *Message, flags RecvFlags) (RecvPartResult, bool, error) {
	result, err := recvRoutedPartInto(out, flags, func(nodeRID **C.zlink_routing_id_t, spotRID **C.zlink_routing_id_t, requestSeq *C.uint64_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_router_recv_part(s.raw(), nodeRID, spotRID, requestSeq, part, hasMore, recvFlags))
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

func (s *routedSocket) startSpotRequest(destNodeRid, destSpotRid RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
	builderParts := make([]requestBuilderPart, len(parts))
	for i, part := range parts {
		builderParts[i] = requestBuilderPart{message: part}
	}
	return s.startSpotRequestBuilder(destNodeRid, destSpotRid, flags, timeout, builderParts)
}

func (s *routedSocket) startSpotRequestBuilder(destNodeRid, destSpotRid RoutingID, flags SendFlags, timeout time.Duration, parts []requestBuilderPart) (*replyCallbackState, error) {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	state := newReplyCallbackState()
	handle := cgo.NewHandle(state)
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	if err := submitMultipartFromRequestParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_request_spot_part_go_local(
			s.raw(),
			&node,
			&spot,
			part,
			C.zlink_send_flags_t(flags),
			partFlag,
			C.uint32_t(requestTimeoutMillis(timeout)),
			C.uintptr_t(handle),
		))
	}); err != nil {
		handle.Delete()
		return nil, err
	}
	startSocketRequestProgress(s.raw(), state)
	return state, nil
}

func (s *RouterSocket) SendTo(target RoutingID) SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		rid := target.toC()
		return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_send_part_rid(s.raw(), &rid, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

func (s *RouterSocket) Request(peerRid RoutingID) RequestOp {
	return newRequestBuilder(nil, func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
		}
		messages, cleanup, err := requestBuilderMessagesForClone(parts)
		if err != nil {
			return err
		}
		defer cleanup()
		resultCh, err := (&routerRequestSupport{socket: s}).startRequest(peerRid, flags, timeout, messages...)
		if err != nil {
			return err
		}
		dispatchRequestCallback(resultCh, callback)
		return nil
	})
}

func (s *RouterSocket) Reply(rid RoutingID, requestSeq uint64) ReplyOp {
	return newReplyBuilder(nil, func(parts []*Message, flags SendFlags) error {
		_, err := (&routerRequestSupport{socket: s}).Reply(rid, requestSeq, flags, parts...)
		return err
	})
}

func (s *RouterSocket) SendToSpot(destNodeRid, destSpotRid RoutingID) SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		node := destNodeRid.toC()
		spot := destSpotRid.toC()
		return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_router_send_spot_part_go_local(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

func (s *RouterSocket) RequestToSpot(destNodeRid, destSpotRid RoutingID) RequestOp {
	return newRequestBuilder(nil, func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
		}
		state, err := s.routedSocket.startSpotRequestBuilder(destNodeRid, destSpotRid, flags, timeout, parts)
		if err != nil {
			return err
		}
		dispatchRequestCallback(state, callback)
		return nil
	})
}

func (s *RouterSocket) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64) ReplyOp {
	return newReplyBuilder(nil, func(parts []*Message, flags SendFlags) error {
		_, err := s.routedSocket.replyToSpot(destNodeRid, destSpotRid, requestSeq, flags, parts...)
		return err
	})
}
