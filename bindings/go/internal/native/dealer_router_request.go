// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include "zlink.h"

extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_dealer_request_part_go_local(void *dealer, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_dealer_request_part(dealer, part, flags, part_flag, timeout_ms, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata);
}

static inline int zlink_router_request_part_go_local(void *router, const zlink_routing_id_t *peer_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_router_request_part(router, peer_rid, part, flags, part_flag, timeout_ms, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata);
}
*/
import "C"

import (
	"runtime/cgo"
	"time"
)

type dealerRequestSupport struct {
	socket *DealerSocket
}

type routerRequestSupport struct {
	socket *RouterSocket
}

func newDealerRequestSupport(socket *DealerSocket) *dealerRequestSupport {
	return &dealerRequestSupport{socket: socket}
}

func (r *dealerRequestSupport) Socket() *DealerSocket { return r.socket }

func (r *dealerRequestSupport) Request(timeout time.Duration, parts ...*Message) ([]*Message, error) {
	state, err := startDealerRequest(r.socket, SendFlagsNone, timeout, parts...)
	if err != nil {
		return nil, err
	}
	result := state.wait()
	if result.result != RequestOK {
		return nil, requestErrorFromResult(result.result)
	}
	return result.parts, nil
}

func (r *dealerRequestSupport) requestCallback(callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	state, err := startDealerRequest(r.socket, flags, timeout, parts...)
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

func (r *dealerRequestSupport) Recv(out *Received, flags RecvFlags) (bool, error) {
	return r.socket.Recv(out, flags)
}

func (r *dealerRequestSupport) onReceive(handler func(*Received)) error {
	return r.socket.onReceive(handler)
}

func (r *dealerRequestSupport) startRequest(flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
	return startDealerRequest(r.socket, flags, timeout, parts...)
}

func startDealerRequest(socket *DealerSocket, flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	state := newReplyCallbackState()
	handle := cgo.NewHandle(state)
	if err := submitPreparedMultipart(prepared, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_dealer_request_part_go_local(
			socket.raw(),
			part,
			C.zlink_send_flags_t(flags),
			partFlag,
			C.uint32_t(requestTimeoutMillis(timeout)),
			C.uintptr_t(handle),
		))
	}); err != nil {
		handle.Delete()
		prepared.commit()
		return nil, err
	}
	prepared.commit()
	socket.socketCore.startRequestProgress(state)
	return state, nil
}

func newRouterRequestSupport(socket *RouterSocket) *routerRequestSupport {
	return &routerRequestSupport{socket: socket}
}

func (r *routerRequestSupport) Socket() *RouterSocket { return r.socket }

func (r *routerRequestSupport) Request(routingID RoutingID, timeout time.Duration, parts ...*Message) ([]*Message, error) {
	state, err := startRouterRequest(r.socket, routingID, SendFlagsNone, timeout, parts...)
	if err != nil {
		return nil, err
	}
	result := state.wait()
	if result.result != RequestOK {
		return nil, requestErrorFromResult(result.result)
	}
	return result.parts, nil
}

func (r *routerRequestSupport) requestCallback(routingID RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	state, err := startRouterRequest(r.socket, routingID, flags, timeout, parts...)
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

func (r *routerRequestSupport) Reply(routingID RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) (bool, error) {
	if err := validateReplyFlags(flags); err != nil {
		return false, err
	}
	rid := routingID.toC()
	err := submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_reply_part(r.socket.raw(), &rid, C.uint64_t(requestSeq), part, partFlag))
	})
	return submitBackpressureResult(err)
}

func (r *routerRequestSupport) startRequest(routingID RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
	return startRouterRequest(r.socket, routingID, flags, timeout, parts...)
}

func startRouterRequest(socket *RouterSocket, routingID RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	state := newReplyCallbackState()
	handle := cgo.NewHandle(state)
	rid := routingID.toC()
	if err := submitPreparedMultipart(prepared, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_request_part_go_local(
			socket.raw(),
			&rid,
			part,
			C.zlink_send_flags_t(flags),
			partFlag,
			C.uint32_t(requestTimeoutMillis(timeout)),
			C.uintptr_t(handle),
		))
	}); err != nil {
		handle.Delete()
		prepared.commit()
		return nil, err
	}
	prepared.commit()
	socket.socketCore.startRequestProgress(state)
	return state, nil
}
