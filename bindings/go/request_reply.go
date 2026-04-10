// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include "zlink.h"
extern void goZlinkReplyTrampoline(int errnum_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkRouterRequestTrampoline(zlink_routing_id_t *peer_rid_, uint64_t request_seq_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_dealer_request_go_local(void *dealer, zlink_msg_t *parts, size_t part_count, uint32_t timeout_ms, uintptr_t userdata) {
	return zlink_dealer_request(dealer, parts, part_count, timeout_ms, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata);
}

static inline int zlink_router_request_go_local(void *router, const zlink_routing_id_t *peer_rid, zlink_msg_t *parts, size_t part_count, uint32_t timeout_ms, uintptr_t userdata) {
	return zlink_router_request(router, peer_rid, parts, part_count, timeout_ms, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata);
}

static inline int zlink_router_handler_go_local(void *router, uintptr_t userdata) {
	return zlink_router_handler(router, (zlink_router_handler_fn)goZlinkRouterRequestTrampoline, (void *)userdata);
}
*/
import "C"

import (
	"context"
	"runtime/cgo"
	"sync"
	"time"
)

const defaultRequestTimeout = 5 * time.Second

type RequestReplyCallback func(*Received, error)

type requestResult struct {
	received *Received
	err      error
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

func (r *RequestDealer) Request(ctx context.Context, parts ...*Message) (*Received, error) {
	return r.requestWith(ctx, parts...)
}

func (r *RequestDealer) TryRequest(ctx context.Context, parts ...*Message) (*Received, error) {
	return r.requestWith(ctx, parts...)
}

func (r *RequestDealer) RequestAsync(timeout time.Duration, callback RequestReplyCallback, parts ...*Message) {
	if callback == nil {
		return
	}
	go func() {
		ctx, cancel := requestContext(timeout)
		defer cancel()
		received, err := r.Request(ctx, parts...)
		callback(received, err)
	}()
}

func (r *RequestDealer) TryRequestAsync(timeout time.Duration, callback RequestReplyCallback, parts ...*Message) {
	r.RequestAsync(timeout, callback, parts...)
}

func (r *RequestDealer) Recv() (*Received, error) { return r.socket.Recv() }

func (r *RequestDealer) TryRecv() (*Received, bool, error) { return r.socket.TryRecv() }

func (r *RequestDealer) OnReceive(handler func(*Received)) error { return r.socket.OnReceive(handler) }

func (r *RequestDealer) Close() error { return r.socket.Close() }

func (r *RequestDealer) requestWith(ctx context.Context, parts ...*Message) (*Received, error) {
	if ctx == nil {
		ctx = context.WithoutCancel(context.Background())
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
	defer handle.Delete()
	if err := checkRC(C.zlink_dealer_request_go_local(
		r.socket.raw(),
		prepared.ptr(),
		prepared.count(),
		C.uint32_t(requestTimeoutMillis(ctx)),
		C.uintptr_t(handle),
	)); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return nil, restoreErr
		}
		return nil, err
	}
	prepared.commit()
	return awaitRequestResult(ctx, resultCh)
}

func NewRequestRouter(socket *RouterSocket) *RequestRouter {
	rr := &RequestRouter{
		socket:    socket,
		dataQueue: make(chan *Received, 64),
	}
	rr.requestHandle = cgo.NewHandle(rr)
	if err := checkRC(C.zlink_router_handler_go_local(socket.raw(), C.uintptr_t(rr.requestHandle))); err != nil {
		rr.requestHandle.Delete()
		panic(err)
	}
	return rr
}

func (r *RequestRouter) Socket() *RouterSocket { return r.socket }

func (r *RequestRouter) Request(ctx context.Context, routingID RoutingID, parts ...*Message) (*Received, error) {
	return r.requestWith(ctx, routingID, parts...)
}

func (r *RequestRouter) TryRequest(ctx context.Context, routingID RoutingID, parts ...*Message) (*Received, error) {
	return r.requestWith(ctx, routingID, parts...)
}

func (r *RequestRouter) RequestAsync(routingID RoutingID, timeout time.Duration, callback RequestReplyCallback, parts ...*Message) {
	if callback == nil {
		return
	}
	go func() {
		ctx, cancel := requestContext(timeout)
		defer cancel()
		received, err := r.Request(ctx, routingID, parts...)
		callback(received, err)
	}()
}

func (r *RequestRouter) TryRequestAsync(routingID RoutingID, timeout time.Duration, callback RequestReplyCallback, parts ...*Message) {
	r.RequestAsync(routingID, timeout, callback, parts...)
}

func (r *RequestRouter) Reply(routingID RoutingID, requestSeq uint64, parts ...*Message) error {
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
	if err := checkRC(C.zlink_router_reply(r.socket.raw(), &rid, C.uint64_t(requestSeq), prepared.ptr(), prepared.count())); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

func (r *RequestRouter) TryReply(routingID RoutingID, requestSeq uint64, parts ...*Message) (SendResult, error) {
	if err := r.Reply(routingID, requestSeq, parts...); err != nil {
		return 0, err
	}
	return SendResultSent, nil
}

func (r *RequestRouter) Recv() (*Received, error) {
	r.handlerMu.RLock()
	handler := r.dataHandler
	r.handlerMu.RUnlock()
	if handler != nil {
		return nil, stateError("socket is in callback mode")
	}
	received, ok := <-r.dataQueue
	if !ok {
		return nil, &ZlinkError{Kind: ErrorKindNative, Code: int(C.ETERM), Message: "socket is closed"}
	}
	return received, nil
}

func (r *RequestRouter) TryRecv() (*Received, bool, error) {
	r.handlerMu.RLock()
	handler := r.dataHandler
	r.handlerMu.RUnlock()
	if handler != nil {
		return nil, false, stateError("socket is in callback mode")
	}
	select {
	case received, ok := <-r.dataQueue:
		if !ok {
			return nil, false, &ZlinkError{Kind: ErrorKindNative, Code: int(C.ETERM), Message: "socket is closed"}
		}
		return received, true, nil
	default:
		return nil, false, nil
	}
}

func (r *RequestRouter) OnReceive(handler func(*Received)) error {
	if handler == nil {
		return validationError("receive handler must not be nil")
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

func (r *RequestRouter) requestWith(ctx context.Context, routingID RoutingID, parts ...*Message) (*Received, error) {
	if ctx == nil {
		ctx = context.WithoutCancel(context.Background())
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
	defer handle.Delete()
	rid := routingID.toC()
	if err := checkRC(C.zlink_router_request_go_local(
		r.socket.raw(),
		&rid,
		prepared.ptr(),
		prepared.count(),
		C.uint32_t(requestTimeoutMillis(ctx)),
		C.uintptr_t(handle),
	)); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return nil, restoreErr
		}
		return nil, err
	}
	prepared.commit()
	return awaitRequestResult(ctx, resultCh)
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

func requestContext(timeout time.Duration) (context.Context, context.CancelFunc) {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	return context.WithTimeout(context.Background(), timeout)
}

func requestTimeoutMillis(ctx context.Context) uint32 {
	if deadline, ok := ctx.Deadline(); ok {
		timeout := time.Until(deadline)
		if timeout <= 0 {
			return 1
		}
		ms := timeout / time.Millisecond
		if ms == 0 {
			ms = 1
		}
		return uint32(ms)
	}
	return uint32(defaultRequestTimeout / time.Millisecond)
}

func awaitRequestResult(ctx context.Context, resultCh <-chan requestResult) (*Received, error) {
	select {
	case result := <-resultCh:
		return result.received, result.err
	case <-ctx.Done():
		if ctx.Err() == context.DeadlineExceeded {
			return nil, &ZlinkError{Kind: ErrorKindNative, Code: int(C.ETIMEDOUT), Message: "request timed out"}
		}
		return nil, ctx.Err()
	}
}

func cloneParts(parts []*Message) ([]*Message, error) {
	if len(parts) == 0 {
		return nil, validationError("multipart payload must contain at least one part")
	}
	cloned := make([]*Message, 0, len(parts))
	for i, part := range parts {
		if part == nil {
			closeMessageSlice(cloned)
			return nil, validationError("part %d is nil", i)
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
func goZlinkReplyTrampoline(errnum C.int, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	state := cgo.Handle(userdata).Value().(*replyCallbackState)
	if errnum != 0 {
		state.result <- requestResult{
			err: &ZlinkError{Kind: ErrorKindNative, Code: int(errnum), Message: C.GoString(C.zlink_strerror(errnum))},
		}
		return
	}
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		state.result <- requestResult{err: err}
		return
	}
	state.result <- requestResult{received: &Received{parts: clonedParts}}
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
