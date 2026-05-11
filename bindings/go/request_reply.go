// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include "zlink.h"

extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern int zlink_socket_request_progress_internal(void *socket_);
extern int zlink_spot_request_progress_internal(void *spot_);

static inline int zlink_dealer_request_part_go_local(void *dealer, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
	return zlink_dealer_request_part(dealer, part, flags, part_flag, timeout_ms, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata);
}

static inline int zlink_router_request_part_go_local(void *router, const zlink_routing_id_t *peer_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
	return zlink_router_request_part(router, peer_rid, part, flags, part_flag, timeout_ms, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata);
}

*/
import "C"

import (
	"errors"
	"runtime/cgo"
	"sync"
	"time"
	"unsafe"
)

const defaultRequestTimeout = 5 * time.Second

type RequestReplyCallback func(RequestResult, []*Message)

type requestResult struct {
	result RequestResult
	parts  []*Message
}

type replyCallbackState struct {
	result chan requestResult
	done   chan struct{}
	once   sync.Once
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

func (r *dealerRequestSupport) requestCallback(callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := startDealerRequest(r.socket, flags, timeout, parts...)
	ok, err := submitBackpressureResult(err)
	if err != nil {
		return false, err
	}
	if !ok {
		return false, nil
	}
	dispatchRequestCallback(resultCh, callback)
	return true, nil
}

func (r *dealerRequestSupport) RequestCallback(callback RequestReplyCallback, timeout time.Duration, parts ...*Message) error {
	_, err := r.requestCallback(callback, SendFlagsNone, timeout, parts...)
	return err
}

func (r *dealerRequestSupport) TryRequestCallback(callback RequestReplyCallback, timeout time.Duration, parts ...*Message) (bool, error) {
	return r.requestCallback(callback, SendFlagsDontWait, timeout, parts...)
}

func (r *dealerRequestSupport) Recv(out *Received, flags RecvFlags) (bool, error) {
	return r.socket.Recv(out, flags)
}

func (r *dealerRequestSupport) onReceive(handler func(*Received)) error {
	return r.socket.onReceive(handler)
}

func (r *dealerRequestSupport) startRequest(flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	return startDealerRequest(r.socket, flags, timeout, parts...)
}

func startDealerRequest(socket *DealerSocket, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	state := &replyCallbackState{
		result: make(chan requestResult, 1),
		done:   make(chan struct{}),
	}
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
	startSocketRequestProgress(socket.raw(), state)
	return state.result, nil
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

func (r *routerRequestSupport) requestCallback(routingID RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := startRouterRequest(r.socket, routingID, flags, timeout, parts...)
	ok, err := submitBackpressureResult(err)
	if err != nil {
		return false, err
	}
	if !ok {
		return false, nil
	}
	dispatchRequestCallback(resultCh, callback)
	return true, nil
}

func (r *routerRequestSupport) RequestCallback(routingID RoutingID, callback RequestReplyCallback, timeout time.Duration, parts ...*Message) error {
	_, err := r.requestCallback(routingID, callback, SendFlagsNone, timeout, parts...)
	return err
}

func (r *routerRequestSupport) TryRequestCallback(routingID RoutingID, callback RequestReplyCallback, timeout time.Duration, parts ...*Message) (bool, error) {
	return r.requestCallback(routingID, callback, SendFlagsDontWait, timeout, parts...)
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

func (r *routerRequestSupport) startRequest(routingID RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	return startRouterRequest(r.socket, routingID, flags, timeout, parts...)
}

func startRouterRequest(socket *RouterSocket, routingID RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	state := &replyCallbackState{
		result: make(chan requestResult, 1),
		done:   make(chan struct{}),
	}
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
	startSocketRequestProgress(socket.raw(), state)
	return state.result, nil
}

func (s *replyCallbackState) complete(result requestResult) {
	s.result <- result
	s.once.Do(func() {
		close(s.done)
	})
}

func startSocketRequestProgress(handle unsafe.Pointer, state *replyCallbackState) {
	startRequestProgress(state, func() {
		C.zlink_socket_request_progress_internal(handle)
	})
}

func startSpotRequestProgress(handle unsafe.Pointer, state *replyCallbackState) {
	startRequestProgress(state, func() {
		C.zlink_spot_request_progress_internal(handle)
	})
}

func startRequestProgress(state *replyCallbackState, step func()) {
	go func() {
		ticker := time.NewTicker(time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-state.done:
				return
			default:
			}
			step()
			select {
			case <-state.done:
				return
			case <-ticker.C:
			}
		}
	}()
}

func requestTimeoutMillis(timeout time.Duration) uint32 {
	if timeout <= 0 {
		return 0
	}
	ms := timeout / time.Millisecond
	if ms == 0 {
		return 1
	}
	if ms > time.Duration(^uint32(0)) {
		return ^uint32(0)
	}
	return uint32(ms)
}

func submitBackpressureResult(err error) (bool, error) {
	if err == nil {
		return true, nil
	}
	var submitErr *SubmitError
	if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
		return false, nil
	}
	return false, err
}

func dispatchRequestCallback(resultCh <-chan requestResult, callback RequestReplyCallback) {
	go func() {
		result := <-resultCh
		callback(result.result, result.parts)
	}()
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
			state.complete(requestResult{result: RequestProtocolError})
			return
		}
		state.complete(requestResult{result: RequestOK, parts: clonedParts})
		return
	}
	state.complete(requestResult{result: RequestResult(result), parts: nil})
}
