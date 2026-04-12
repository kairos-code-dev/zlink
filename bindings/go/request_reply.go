// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include "zlink.h"

extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkRouterRequestTrampoline(zlink_routing_id_t *peer_rid_, uint64_t request_seq_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_dealer_request_go_local(void *dealer, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
	return zlink_dealer_request(dealer, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_router_request_go_local(void *router, const zlink_routing_id_t *peer_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
	return zlink_router_request(router, peer_rid, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_router_handler_go_local(void *router, uintptr_t userdata) {
	return zlink_router_handler(router, (zlink_router_handler_fn)goZlinkRouterRequestTrampoline, (void *)userdata);
}
*/
import "C"

import (
	"runtime/cgo"
	"sync"
	"time"
)

const defaultRequestTimeout = 5 * time.Second

type RequestReplyCallback func(RequestResult, *Received)

type requestResult struct {
	result   RequestResult
	received *Received
}

type replyCallbackState struct {
	result chan requestResult
}

type RequestDealer struct {
	socket *DealerSocket
}

type RequestRouter struct {
	socket        *RouterSocket
	dataQueue     chan *Received
	closeOnce     sync.Once
	handlerMu     sync.RWMutex
	dataHandler   func(*Received)
	requestHandle cgo.Handle
}

func NewRequestDealer(socket *DealerSocket) *RequestDealer {
	return &RequestDealer{socket: socket}
}

func (r *RequestDealer) Socket() *DealerSocket { return r.socket }

func (r *RequestDealer) Request(timeout time.Duration, parts ...*Message) (*Received, error) {
	resultCh, err := r.startRequest(SendFlagsNone, timeout, parts...)
	if err != nil {
		return nil, err
	}
	result := <-resultCh
	if result.result != RequestOK {
		return nil, requestErrorFromResult(result.result)
	}
	return result.received, nil
}

func (r *RequestDealer) RequestCallback(callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) error {
	if callback == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := r.startRequest(flags, timeout, parts...)
	if err != nil {
		return err
	}
	go func() {
		result := <-resultCh
		callback(result.result, result.received)
	}()
	return nil
}

func (r *RequestDealer) Recv(flags RecvFlags) (*Received, error) { return r.socket.Recv(flags) }

func (r *RequestDealer) OnReceive(handler func(*Received)) error { return r.socket.OnReceive(handler) }

func (r *RequestDealer) Close() error { return r.socket.Close() }

func (r *RequestDealer) startRequest(flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
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
		r.socket.raw(),
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

func NewRequestRouter(socket *RouterSocket) *RequestRouter {
	rr := &RequestRouter{
		socket:    socket,
		dataQueue: make(chan *Received, 64),
	}
	rr.requestHandle = cgo.NewHandle(rr)
	if err := handlerErrorFromResult(C.zlink_router_handler_go_local(socket.raw(), C.uintptr_t(rr.requestHandle))); err != nil {
		rr.requestHandle.Delete()
		panic(err)
	}
	return rr
}

func (r *RequestRouter) Socket() *RouterSocket { return r.socket }

func (r *RequestRouter) Request(routingID RoutingID, timeout time.Duration, parts ...*Message) (*Received, error) {
	resultCh, err := r.startRequest(routingID, SendFlagsNone, timeout, parts...)
	if err != nil {
		return nil, err
	}
	result := <-resultCh
	if result.result != RequestOK {
		return nil, requestErrorFromResult(result.result)
	}
	return result.received, nil
}

func (r *RequestRouter) RequestCallback(routingID RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) error {
	if callback == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := r.startRequest(routingID, flags, timeout, parts...)
	if err != nil {
		return err
	}
	go func() {
		result := <-resultCh
		callback(result.result, result.received)
	}()
	return nil
}

func (r *RequestRouter) Reply(routingID RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
	cloned, err := cloneParts(parts)
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return err
	}
	rid := routingID.toC()
	if err := submitErrorFromResult(C.zlink_router_reply(r.socket.raw(), &rid, C.uint64_t(requestSeq), prepared.ptr(), prepared.count())); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

func (r *RequestRouter) Recv(flags RecvFlags) (*Received, error) {
	r.handlerMu.RLock()
	handler := r.dataHandler
	r.handlerMu.RUnlock()
	if handler != nil {
		return nil, &RecvError{Result: RecvBusy, internalErrno: int(C.EBUSY)}
	}
	select {
	case received, ok := <-r.dataQueue:
		if !ok {
			return nil, &RecvError{Result: RecvTerminated, internalErrno: int(C.ETERM)}
		}
		return received, nil
	default:
		if flags == RecvFlagsDontWait {
			return nil, &RecvError{Result: RecvNoData, internalErrno: int(C.EAGAIN)}
		}
	}
	received, ok := <-r.dataQueue
	if !ok {
		return nil, &RecvError{Result: RecvTerminated, internalErrno: int(C.ETERM)}
	}
	return received, nil
}

func (r *RequestRouter) OnReceive(handler func(*Received)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	r.handlerMu.Lock()
	r.dataHandler = handler
	r.handlerMu.Unlock()
	return nil
}

func (r *RequestRouter) Close() error {
	var err error
	r.closeOnce.Do(func() {
		if r.requestHandle != 0 {
			r.requestHandle.Delete()
			r.requestHandle = 0
		}
		err = r.socket.Close()
		close(r.dataQueue)
	})
	return err
}

func (r *RequestRouter) startRequest(routingID RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
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
	rid := routingID.toC()
	if err := submitErrorFromResult(C.zlink_router_request_go_local(
		r.socket.raw(),
		&rid,
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

func (r *RequestRouter) deliverRequest(received *Received) {
	r.handlerMu.RLock()
	handler := r.dataHandler
	r.handlerMu.RUnlock()
	if handler != nil {
		handler(received)
		return
	}
	select {
	case r.dataQueue <- received:
	default:
		_ = received.Close()
	}
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
	state := cgo.Handle(userdata).Value().(*replyCallbackState)
	received := &Received{}
	if result == C.ZLINK_REQUEST_OK {
		clonedParts, err := takeParts(parts, partCount)
		if err != nil {
			state.result <- requestResult{result: RequestProtocolError}
			return
		}
		received.parts = clonedParts
		state.result <- requestResult{result: RequestOK, received: received}
		return
	}
	state.result <- requestResult{result: RequestResult(result)}
}

//export goZlinkRouterRequestTrampoline
func goZlinkRouterRequestTrampoline(peerRID *C.zlink_routing_id_t, requestSeq C.uint64_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	router := cgo.Handle(userdata).Value().(*RequestRouter)
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		return
	}
	received := &Received{
		routingID:     routingIDFromC(*peerRID),
		parts:         clonedParts,
		requestSeq:    uint64(requestSeq),
		hasRequestSeq: true,
	}
	router.deliverRequest(received)
}
