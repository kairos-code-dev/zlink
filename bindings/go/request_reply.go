// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include "zlink.h"

extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_dealer_request_go_local(void *dealer, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
	return zlink_dealer_request(dealer, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_router_request_go_local(void *router, const zlink_routing_id_t *peer_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
	return zlink_router_request(router, peer_rid, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_router_request_go_bytes_local(void *router, const void *data, size_t size, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
	zlink_routing_id_t routing_id;
	routing_id.size = size;
	routing_id.data = (uint8_t *)data;
	return zlink_router_request(router, &routing_id, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_router_reply_go_local(void *router, const void *data, size_t size, uint64_t request_seq, zlink_msg_t *parts, size_t part_count) {
	zlink_routing_id_t routing_id;
	routing_id.size = size;
	routing_id.data = (uint8_t *)data;
	return zlink_router_reply(router, &routing_id, request_seq, parts, part_count);
}

*/
import "C"

import (
	"runtime/cgo"
	"time"
)

const defaultRequestTimeout = 5 * time.Second

type RequestReplyCallback func(RequestResult, []*Message)

type requestResult struct {
	result RequestResult
	parts  []*Message
}

type replyCallbackState struct {
	result chan requestResult
}

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
	resultCh, err := startDealerRequest(r.socket, SendFlagsNone, timeout, parts...)
	if err != nil {
		return nil, err
	}
	result := <-resultCh
	if result.result != RequestOK {
		return nil, requestErrorFromResult(result.result)
	}
	return result.parts, nil
}

func (r *dealerRequestSupport) RequestCallback(callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) error {
	if callback == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := startDealerRequest(r.socket, flags, timeout, parts...)
	if err != nil {
		return err
	}
	go func() {
		result := <-resultCh
		callback(result.result, result.parts)
	}()
	return nil
}

func (r *dealerRequestSupport) Recv(flags RecvFlags) (*Received, error) { return r.socket.Recv(flags) }

func (r *dealerRequestSupport) onReceive(handler func(*Received)) error { return r.socket.onReceive(handler) }

func (r *dealerRequestSupport) startRequest(flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	return startDealerRequest(r.socket, flags, timeout, parts...)
}

func startDealerRequest(socket *DealerSocket, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	resultCh := make(chan requestResult, 1)
	handle := cgo.NewHandle(&replyCallbackState{result: resultCh})
	if err := submitErrorFromResult(C.zlink_dealer_request_go_local(
		socket.raw(),
		prepared.ptr(),
		prepared.count(),
		C.zlink_send_flags_t(flags),
		C.uint32_t(requestTimeoutMillis(timeout)),
		C.uintptr_t(handle),
	)); err != nil {
		handle.Delete()
		if restoreErr := prepared.restore(); restoreErr != nil {
			return nil, restoreErr
		}
		return nil, err
	}
	prepared.commit()
	return resultCh, nil
}

func newRouterRequestSupport(socket *RouterSocket) *routerRequestSupport {
	return &routerRequestSupport{socket: socket}
}

func (r *routerRequestSupport) Socket() *RouterSocket { return r.socket }

func (r *routerRequestSupport) Request(routingID RoutingID, timeout time.Duration, parts ...*Message) ([]*Message, error) {
	resultCh, err := startRouterRequest(r.socket, routingID, SendFlagsNone, timeout, parts...)
	if err != nil {
		return nil, err
	}
	result := <-resultCh
	if result.result != RequestOK {
		return nil, requestErrorFromResult(result.result)
	}
	return result.parts, nil
}

func (r *routerRequestSupport) RequestCallback(routingID RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) error {
	if callback == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := startRouterRequest(r.socket, routingID, flags, timeout, parts...)
	if err != nil {
		return err
	}
	go func() {
		result := <-resultCh
		callback(result.result, result.parts)
	}()
	return nil
}

func (r *routerRequestSupport) Reply(routingID RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
	if err := validateReplyFlags(flags); err != nil {
		return err
	}
	cloned, err := cloneParts(parts)
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return err
	}
	if err := submitErrorFromResult(C.zlink_router_reply_go_local(r.socket.raw(), routingIDBytesPointer(routingID), C.size_t(routingID.Size()), C.uint64_t(requestSeq), prepared.ptr(), prepared.count())); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

func (r *routerRequestSupport) startRequest(routingID RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	return startRouterRequest(r.socket, routingID, flags, timeout, parts...)
}

func startRouterRequest(socket *RouterSocket, routingID RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	resultCh := make(chan requestResult, 1)
	handle := cgo.NewHandle(&replyCallbackState{result: resultCh})
	if err := submitErrorFromResult(C.zlink_router_request_go_bytes_local(
		socket.raw(),
		routingIDBytesPointer(routingID),
		C.size_t(routingID.Size()),
		prepared.ptr(),
		prepared.count(),
		C.zlink_send_flags_t(flags),
		C.uint32_t(requestTimeoutMillis(timeout)),
		C.uintptr_t(handle),
	)); err != nil {
		handle.Delete()
		if restoreErr := prepared.restore(); restoreErr != nil {
			return nil, restoreErr
		}
		return nil, err
	}
	prepared.commit()
	return resultCh, nil
}

func requestTimeoutMillis(timeout time.Duration) uint32 {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	ms := timeout / time.Millisecond
	if ms == 0 {
		ms = 1
	}
	return uint32(ms)
}

func cloneParts(parts []*Message) ([]*Message, error) {
	if len(parts) == 0 {
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	cloned := make([]*Message, 0, len(parts))
	for _, part := range parts {
		if part == nil {
			closeMessageSlice(cloned)
			return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		dup, err := part.clone()
		if err != nil {
			closeMessageSlice(cloned)
			return nil, err
		}
		cloned = append(cloned, dup)
	}
	return cloned, nil
}

//export goZlinkReplyTrampoline
func goZlinkReplyTrampoline(result C.zlink_request_result_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	handle := cgo.Handle(userdata)
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	defer handle.Delete()
	state := value.(*replyCallbackState)
	if result == C.ZLINK_REQUEST_OK {
		clonedParts, err := takeParts(parts, partCount)
		if err != nil {
			state.result <- requestResult{result: RequestProtocolError}
			return
		}
		state.result <- requestResult{result: RequestOK, parts: clonedParts}
		return
	}
	state.result <- requestResult{result: RequestResult(result), parts: nil}
}
