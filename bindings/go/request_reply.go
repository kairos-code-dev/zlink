// SPDX-License-Identifier: MPL-2.0

package zlink

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
	"errors"
	"runtime"
	"runtime/cgo"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"
)

const defaultRequestTimeout = 5 * time.Second

type RequestReplyCallback func(RequestResult, []*Message)

type RequestReplyCompletion struct {
	Result RequestResult
	Parts  []*Message
	Err    error
}

type requestResult struct {
	result RequestResult
	parts  []*Message
}

type replyCallbackState struct {
	result   chan requestResult
	done     chan struct{}
	once     sync.Once
	metadata any
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

// Per-handle progress pump: a single goroutine drives request progress for
// all in-flight requests on the same native handle, using spin-then-exponential
// backoff (matches doc/spec/bindings/go/README.md §Performance Policy: do not
// start one polling thread per request when progress can be shared per handle).
const (
    pollCompletionEvent = C.short(32)
)

type progressPump struct {
	handle      unsafe.Pointer
	activeCount int64
	workerOn    int32
}

var (
	socketProgressPumps sync.Map // unsafe.Pointer -> *progressPump
	spotProgressPumps   sync.Map
)

func startSocketRequestProgress(handle unsafe.Pointer, state *replyCallbackState) {
	getOrCreateProgressPump(&socketProgressPumps, handle).attachDone(state.done)
}

func startSpotRequestProgress(handle unsafe.Pointer, state *replyCallbackState) {
	getOrCreateProgressPump(&spotProgressPumps, handle).attachDone(state.done)
}

// attachSpotProgressDone wires arbitrary operation-completion channels (e.g.
// actor lookup) onto the shared per-handle progress pump for a spot. The
// caller owns `done` and must close it when the operation completes.
func attachSpotProgressDone(handle unsafe.Pointer, done <-chan struct{}) {
	getOrCreateProgressPump(&spotProgressPumps, handle).attachDone(done)
}

func getOrCreateProgressPump(m *sync.Map, handle unsafe.Pointer) *progressPump {
	if v, ok := m.Load(handle); ok {
		return v.(*progressPump)
	}
	p := &progressPump{handle: handle}
	actual, _ := m.LoadOrStore(handle, p)
	return actual.(*progressPump)
}

func (p *progressPump) attachDone(done <-chan struct{}) {
	atomic.AddInt64(&p.activeCount, 1)
	go func() {
		<-done
		atomic.AddInt64(&p.activeCount, -1)
	}()
	if atomic.CompareAndSwapInt32(&p.workerOn, 0, 1) {
		go p.run()
	}
}

func (p *progressPump) run() {
	defer atomic.StoreInt32(&p.workerOn, 0)
	poller := C.zlink_poller_new()
	if poller == nil {
		return
	}
	defer C.zlink_poller_destroy(&poller)
	if C.zlink_poller_add(poller, p.handle, nil, pollCompletionEvent) != C.ZLINK_CONFIG_OK {
		return
	}
	defer C.zlink_poller_remove(poller, p.handle)
	var event C.zlink_poller_event_t
	for atomic.LoadInt64(&p.activeCount) > 0 {
		C.zlink_poller_wait(poller, &event, 1, -1, nil)
		runtime.Gosched()
	}
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
