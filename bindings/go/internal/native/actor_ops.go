// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "zlink.h"

*/
import "C"

import (
	"context"
	"errors"
	"runtime/cgo"
	"time"
	"unsafe"
)

// --- value structs ---

// ActorJoinResult is the completion value of an Actor join operation.
type ActorJoinResult struct {
	Result         RequestResult
	JoinResultCode int32
	Actor          ActorRef
	JoinedSpotRID  RoutingID
	JoinEpoch      uint64
	Flags          uint32
}

// ActorJoinEntrySpotResult is the completion value of an Entry Spot join
// operation.
type ActorJoinEntrySpotResult struct {
	Result        RequestResult
	Actor         ActorRef
	TargetNodeRID RoutingID
	JoinEpoch     uint64
	Flags         uint32
}

type ActorJoinCompletion struct {
	Result ActorJoinResult
	Parts  []*Message
	Err    error
}

type ActorJoinEntrySpotCompletion struct {
	Result ActorJoinEntrySpotResult
	Err    error
}

// ActorLookupResult is the completion value of RemoteActorGetRef.
type ActorLookupResult struct {
	Result RequestResult
	Actor  ActorRef
	Flags  uint32
}

type ActorLookupCompletion struct {
	Result ActorLookupResult
	Err    error
}

// SpotActorLifecycleInfo describes one Spot Actor lifecycle transition.
type SpotActorLifecycleInfo struct {
	PreviousActor   ActorRef
	CurrentActor    ActorRef
	PreviousSpotRID *RoutingID
	CurrentSpotRID  *RoutingID
	JoinEpoch       uint64
	Flags           uint32
}

type SpotActorLifecycleEventKind int

const (
	SpotActorLifecycleJoined SpotActorLifecycleEventKind = 1
	SpotActorLifecycleLeft   SpotActorLifecycleEventKind = 2
)

type SpotActorLifecycleEvent struct {
	Kind SpotActorLifecycleEventKind
	Info SpotActorLifecycleInfo
}

// --- builder interfaces ---

// ActorJoinOp is returned by SpotNode.JoinActor and Actor.Join. The first
// .Message(...) call advances to ActorJoinSubmitOp.
type ActorJoinOp interface {
	Message(message *Message) ActorJoinSubmitOp
}

// ActorJoinSubmitOp is the join operation builder after the first part is
// attached. SubmitAsync returns the completion channel; Flags advances to the
// callback-only stage.
type ActorJoinSubmitOp interface {
	Message(message *Message) ActorJoinSubmitOp
	Timeout(timeout time.Duration) ActorJoinSubmitOp
	Flags(flags SendFlags) ActorJoinCallbackSubmitOp
	SubmitAsync(ctx context.Context) (<-chan ActorJoinCompletion, error)
	Submit(ctx context.Context,
		callback func(result ActorJoinResult, parts []*Message)) (bool, error)
}

// ActorJoinCallbackSubmitOp is the callback-only stage of the join builder.
type ActorJoinCallbackSubmitOp interface {
	Message(message *Message) ActorJoinCallbackSubmitOp
	Timeout(timeout time.Duration) ActorJoinCallbackSubmitOp
	Flags(flags SendFlags) ActorJoinCallbackSubmitOp
	Submit(ctx context.Context,
		callback func(result ActorJoinResult, parts []*Message)) (bool, error)
}

// ActorJoinEntrySpotOp is returned by SpotNode.JoinActorEntrySpot.
type ActorJoinEntrySpotOp interface {
	Timeout(timeout time.Duration) ActorJoinEntrySpotOp
	SubmitAsync(ctx context.Context) (<-chan ActorJoinEntrySpotCompletion, error)
	Submit(ctx context.Context,
		callback func(result ActorJoinEntrySpotResult)) (bool, error)
}

// ActorJoinReplyOp is returned by Spot.ReplyActorJoin. The reply payload is
// optional; Submit may be called with zero attached parts.
type ActorJoinReplyOp interface {
	Message(message *Message) ActorJoinReplyOp
	Submit(ctx context.Context) error
}

// requestPartsAsyncOp is the canonical shape for actor async operations that
// return RequestReplyCompletion. Aliased to the per-operation public names so
// callers keep their familiar entrypoints (SpotNode.LeaveActor, StreamSocket.
// BindActor, ...) without the binding having to repeat identical builder
// hierarchies for each one.
type requestPartsAsyncOp interface {
	Timeout(timeout time.Duration) requestPartsAsyncOp
	SubmitAsync(ctx context.Context) (<-chan RequestReplyCompletion, error)
	Submit(ctx context.Context,
		callback func(result RequestResult, parts []*Message)) (bool, error)
}

// ActorLeaveOp is returned by SpotNode.LeaveActor / Actor.Leave.
type ActorLeaveOp = requestPartsAsyncOp

// ActorDestroyOp is returned by SpotNode.DestroyActor.
type ActorDestroyOp = requestPartsAsyncOp

// ActorBindOp is returned by StreamSocket.BindActor.
type ActorBindOp = requestPartsAsyncOp

// ActorUnbindOp is returned by StreamSocket.UnbindActor.
type ActorUnbindOp = requestPartsAsyncOp

// ActorLookupOp is returned by SpotNode.RemoteActorGetRef.
type ActorLookupOp interface {
	Timeout(timeout time.Duration) ActorLookupOp
	SubmitAsync(ctx context.Context) (<-chan ActorLookupCompletion, error)
	Submit(ctx context.Context,
		callback func(result ActorLookupResult)) (bool, error)
}

// --- common state ---

type actorJoinCallback func(result ActorJoinResult, parts []*Message)
type actorJoinEntrySpotCallback func(result ActorJoinEntrySpotResult)
type actorLookupCallback func(result ActorLookupResult)
type requestPartsCallback func(result RequestResult, parts []*Message)

type actorJoinCallbackState struct {
	result chan actorJoinTrampolineResult
	done   chan struct{}
	once   completionGuard
}

type actorJoinTrampolineResult struct {
	result ActorJoinResult
	parts  []*Message
}

type actorJoinEntrySpotCallbackState struct {
	result chan ActorJoinEntrySpotResult
	done   chan struct{}
	once   completionGuard
}

type actorLookupCallbackState struct {
	result chan ActorLookupResult
	done   chan struct{}
	once   completionGuard
}

type completionGuard struct {
	closed bool
}

func (g *completionGuard) close(done chan struct{}) {
	if g.closed {
		return
	}
	g.closed = true
	close(done)
}

// --- join builder implementation ---

type actorJoinBuilderState struct {
	parts     []*Message
	flags     SendFlags
	timeout   time.Duration
	submitted bool
	submit    func(parts []*Message, flags SendFlags, timeout time.Duration, cb actorJoinCallback) error
}

type actorJoinBuilder struct {
	state *actorJoinBuilderState
}

type actorJoinSubmitBuilder struct {
	state *actorJoinBuilderState
}

type actorJoinCallbackBuilder struct {
	state *actorJoinBuilderState
}

func newActorJoinOp(submit func(parts []*Message, flags SendFlags, timeout time.Duration, cb actorJoinCallback) error) ActorJoinOp {
	return &actorJoinBuilder{state: &actorJoinBuilderState{submit: submit}}
}

type actorJoinEntrySpotBuilderState struct {
	timeout   time.Duration
	submitted bool
	submit    func(timeout time.Duration, cb actorJoinEntrySpotCallback) error
}

type actorJoinEntrySpotBuilder struct {
	state *actorJoinEntrySpotBuilderState
}

func newActorJoinEntrySpotOp(submit func(timeout time.Duration, cb actorJoinEntrySpotCallback) error) ActorJoinEntrySpotOp {
	return &actorJoinEntrySpotBuilder{state: &actorJoinEntrySpotBuilderState{submit: submit}}
}

func (b *actorJoinEntrySpotBuilder) Timeout(timeout time.Duration) ActorJoinEntrySpotOp {
	b.state.timeout = timeout
	return b
}

func (b *actorJoinEntrySpotBuilder) SubmitAsync(_ context.Context) (<-chan ActorJoinEntrySpotCompletion, error) {
	resultCh := make(chan ActorJoinEntrySpotCompletion, 1)
	ok, err := b.Submit(context.Background(), func(result ActorJoinEntrySpotResult) {
		completion := ActorJoinEntrySpotCompletion{Result: result}
		if result.Result != RequestOK {
			completion.Err = &RequestError{Result: result.Result}
		}
		resultCh <- completion
		close(resultCh)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return resultCh, nil
}

func (b *actorJoinEntrySpotBuilder) Submit(_ context.Context, callback func(ActorJoinEntrySpotResult)) (bool, error) {
	if b.state.submitted {
		return false, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	b.state.submitted = true
	if err := b.state.submit(b.state.timeout, callback); err != nil {
		return false, err
	}
	return true, nil
}

func (b *actorJoinBuilder) Message(msg *Message) ActorJoinSubmitOp {
	b.state.parts = append(b.state.parts, msg)
	return &actorJoinSubmitBuilder{state: b.state}
}

func (b *actorJoinSubmitBuilder) Message(msg *Message) ActorJoinSubmitOp {
	b.state.parts = append(b.state.parts, msg)
	return b
}

func (b *actorJoinSubmitBuilder) Timeout(timeout time.Duration) ActorJoinSubmitOp {
	b.state.timeout = timeout
	return b
}

func (b *actorJoinSubmitBuilder) Flags(flags SendFlags) ActorJoinCallbackSubmitOp {
	b.state.flags = flags
	return &actorJoinCallbackBuilder{state: b.state}
}

func (b *actorJoinSubmitBuilder) SubmitAsync(_ context.Context) (<-chan ActorJoinCompletion, error) {
	return b.state.doSubmitAsync()
}

func (b *actorJoinSubmitBuilder) Submit(_ context.Context, callback func(ActorJoinResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (b *actorJoinCallbackBuilder) Message(msg *Message) ActorJoinCallbackSubmitOp {
	b.state.parts = append(b.state.parts, msg)
	return b
}

func (b *actorJoinCallbackBuilder) Timeout(timeout time.Duration) ActorJoinCallbackSubmitOp {
	b.state.timeout = timeout
	return b
}

func (b *actorJoinCallbackBuilder) Flags(flags SendFlags) ActorJoinCallbackSubmitOp {
	b.state.flags = flags
	return b
}

func (b *actorJoinCallbackBuilder) Submit(_ context.Context, callback func(ActorJoinResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (s *actorJoinBuilderState) doSubmitAsync() (<-chan ActorJoinCompletion, error) {
	resultCh := make(chan ActorJoinCompletion, 1)
	ok, err := s.doSubmitCallback(func(result ActorJoinResult, parts []*Message) {
		completion := ActorJoinCompletion{Result: result, Parts: parts}
		if result.Result != RequestOK {
			completion.Err = &RequestError{Result: result.Result}
		}
		resultCh <- completion
		close(resultCh)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return resultCh, nil
}

func (s *actorJoinBuilderState) doSubmitCallback(callback func(ActorJoinResult, []*Message)) (bool, error) {
	if s.submitted {
		return false, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if len(s.parts) == 0 || callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	s.submitted = true
	if err := s.submit(s.parts, s.flags, s.timeout, callback); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

// --- join reply builder ---

type actorJoinReplyBuilder struct {
	parts     []*Message
	submitted bool
	submit    func(parts []*Message) error
}

func newActorJoinReplyOp(submit func(parts []*Message) error) ActorJoinReplyOp {
	return &actorJoinReplyBuilder{submit: submit}
}

func (b *actorJoinReplyBuilder) Message(msg *Message) ActorJoinReplyOp {
	b.parts = append(b.parts, msg)
	return b
}

func (b *actorJoinReplyBuilder) Submit(_ context.Context) error {
	if b.submitted {
		return &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	b.submitted = true
	return b.submit(b.parts)
}

// --- payload-less request-style builder (leave / destroy / bind / unbind) ---

type requestPartsBuilderState struct {
	timeout   time.Duration
	submitted bool
	submit    func(timeout time.Duration, cb requestPartsCallback) error
}

func (s *requestPartsBuilderState) doSubmitAsync() (<-chan RequestReplyCompletion, error) {
	resultCh := make(chan RequestReplyCompletion, 1)
	ok, err := s.doSubmitCallback(func(r RequestResult, parts []*Message) {
		completion := RequestReplyCompletion{Result: r, Parts: parts}
		if r != RequestOK {
			completion.Err = &RequestError{Result: r}
		}
		resultCh <- completion
		close(resultCh)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return resultCh, nil
}

func (s *requestPartsBuilderState) doSubmitCallback(callback requestPartsCallback) (bool, error) {
	if s.submitted {
		return false, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	s.submitted = true
	if err := s.submit(s.timeout, callback); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

// requestPartsBuilder is the canonical implementation that backs
// ActorLeaveOp, ActorBindOp, and ActorUnbindOp. They all share the same
// timeout+submit shape, so they share one concrete builder; the public
// type aliases above keep the per-operation names on the user API.
type requestPartsBuilder struct {
	state *requestPartsBuilderState
}

func (b *requestPartsBuilder) Timeout(timeout time.Duration) requestPartsAsyncOp {
	b.state.timeout = timeout
	return b
}

func (b *requestPartsBuilder) SubmitAsync(_ context.Context) (<-chan RequestReplyCompletion, error) {
	return b.state.doSubmitAsync()
}

func (b *requestPartsBuilder) Submit(_ context.Context, cb func(RequestResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(cb)
}

func newActorLeaveOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorLeaveOp {
	return &requestPartsBuilder{state: &requestPartsBuilderState{submit: submit}}
}

// actorDestroyBuilder

type actorDestroyBuilder struct {
	state     *requestPartsBuilderState
	onSuccess func()
}

func newActorDestroyOp(onSuccess func(), submit func(timeout time.Duration, cb requestPartsCallback) error) ActorDestroyOp {
	return &actorDestroyBuilder{state: &requestPartsBuilderState{submit: submit}, onSuccess: onSuccess}
}

func (b *actorDestroyBuilder) Timeout(timeout time.Duration) ActorDestroyOp {
	b.state.timeout = timeout
	return b
}

func (b *actorDestroyBuilder) SubmitAsync(ctx context.Context) (<-chan RequestReplyCompletion, error) {
	resultCh := make(chan RequestReplyCompletion, 1)
	ok, err := b.Submit(ctx, func(r RequestResult, parts []*Message) {
		completion := RequestReplyCompletion{Result: r, Parts: parts}
		if r != RequestOK {
			completion.Err = &RequestError{Result: r}
		}
		resultCh <- completion
		close(resultCh)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return resultCh, nil
}

func (b *actorDestroyBuilder) Submit(_ context.Context, cb func(RequestResult, []*Message)) (bool, error) {
	wrapped := cb
	if b.onSuccess != nil {
		wrapped = func(r RequestResult, parts []*Message) {
			if r == RequestOK {
				b.onSuccess()
			}
			cb(r, parts)
		}
	}
	return b.state.doSubmitCallback(wrapped)
}

func newActorBindOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorBindOp {
	return &requestPartsBuilder{state: &requestPartsBuilderState{submit: submit}}
}

func newActorUnbindOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorUnbindOp {
	return &requestPartsBuilder{state: &requestPartsBuilderState{submit: submit}}
}

// --- lookup builder ---

type actorLookupBuilderState struct {
	timeout   time.Duration
	submitted bool
	submit    func(timeout time.Duration, cb actorLookupCallback) error
}

type actorLookupBuilder struct {
	state *actorLookupBuilderState
}

func newActorLookupOp(submit func(timeout time.Duration, cb actorLookupCallback) error) ActorLookupOp {
	return &actorLookupBuilder{state: &actorLookupBuilderState{submit: submit}}
}

func (b *actorLookupBuilder) Timeout(timeout time.Duration) ActorLookupOp {
	b.state.timeout = timeout
	return b
}

func (b *actorLookupBuilder) SubmitAsync(ctx context.Context) (<-chan ActorLookupCompletion, error) {
	resultCh := make(chan ActorLookupCompletion, 1)
	ok, err := b.Submit(ctx, func(result ActorLookupResult) {
		completion := ActorLookupCompletion{Result: result}
		if result.Result != RequestOK {
			completion.Err = &RequestError{Result: result.Result}
		}
		resultCh <- completion
		close(resultCh)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return resultCh, nil
}

func (b *actorLookupBuilder) Submit(_ context.Context, cb func(ActorLookupResult)) (bool, error) {
	if b.state.submitted {
		return false, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if cb == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	b.state.submitted = true
	if err := b.state.submit(b.state.timeout, cb); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

// --- shared submission helpers ---

// submitActorJoinNative drives the native join handler. It clones the parts,
// converts them to a native multipart, registers a reply-callback state and
// hands ownership of the parts to the native call. The trampoline pushes the
// ActorJoinResult and reply parts back through the channel which the caller
// goroutine forwards to the user callback.
func submitActorJoinNative(parts []*Message, progressSpot unsafe.Pointer, native func(nativeParts *C.zlink_msg_t, partCount C.size_t, cb cgo.Handle) error, callback actorJoinCallback) error {
	cloned, err := cloneParts(parts)
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return err
	}
	state := newReplyCallbackState()
	state.metadata = &actorJoinMetadata{callback: callback}
	handle := cgo.NewHandle(state)
	if err := native(prepared.ptr(), prepared.count(), handle); err != nil {
		handle.Delete()
		_ = prepared.restore()
		return err
	}
	prepared.commit()
	markPartsMoved(parts)
	if progressSpot != nil {
		startSpotRequestProgress(progressSpot, state)
	}
	go func() {
		result := state.wait()
		meta, _ := state.metadata.(*actorJoinMetadata)
		joinResult := meta.joinResult
		joinResult.Result = result.result
		callback(joinResult, result.parts)
	}()
	return nil
}

// actorJoinMetadata is attached to the reply callback state so the
// join-trampoline can stash the actor-specific result fields before
// completion is signalled.
type actorJoinMetadata struct {
	callback   actorJoinCallback
	joinResult ActorJoinResult
}

// submitActorRequestNative drives a generic reply-only actor operation
// (destroy, leave, bind, unbind). It wires the user callback to the standard
// reply trampoline.
func submitActorRequestNative(progressSpot unsafe.Pointer, native func(cb cgo.Handle) error, callback requestPartsCallback) error {
	state := newReplyCallbackState()
	handle := cgo.NewHandle(state)
	if err := native(handle); err != nil {
		handle.Delete()
		return err
	}
	if progressSpot != nil {
		startSpotRequestProgress(progressSpot, state)
	}
	go func() {
		result := state.wait()
		callback(result.result, result.parts)
	}()
	return nil
}

// submitActorLookupNative drives the actor lookup operation. The lookup
// trampoline carries the lookup result through a dedicated channel.
// Progress is driven by the shared per-handle progress pump (same one the
// reply-style actor operations and dealer/router requests use), not by a
// dedicated goroutine + poller_wait(-1) per call.
func submitActorLookupNative(progressSpot unsafe.Pointer, native func(cb cgo.Handle) error, callback actorLookupCallback) error {
	state := &actorLookupCallbackState{
		result: make(chan ActorLookupResult, 1),
		done:   make(chan struct{}),
	}
	handle := cgo.NewHandle(state)
	if err := native(handle); err != nil {
		handle.Delete()
		return err
	}
	if progressSpot != nil {
		attachSpotProgressDone(progressSpot, state.done)
	}
	go func() {
		result := <-state.result
		callback(result)
	}()
	return nil
}

func submitActorJoinEntrySpotNative(native func(cb cgo.Handle) error, callback actorJoinEntrySpotCallback) error {
	state := &actorJoinEntrySpotCallbackState{
		result: make(chan ActorJoinEntrySpotResult, 1),
		done:   make(chan struct{}),
	}
	handle := cgo.NewHandle(state)
	if err := native(handle); err != nil {
		handle.Delete()
		return err
	}
	go func() {
		result := <-state.result
		callback(result)
	}()
	return nil
}

//export goZlinkActorLookupTrampoline
func goZlinkActorLookupTrampoline(result *C.zlink_actor_lookup_result_t, userdata C.uintptr_t) {
	handle := cgo.Handle(userdata)
	state, ok := safeHandleAs[*actorLookupCallbackState](userdata)
	if !ok || state == nil {
		return
	}
	defer handle.Delete()
	var out ActorLookupResult
	if result != nil {
		out = ActorLookupResult{
			Result: RequestResult(result.result),
			Actor:  actorRefFromC(result.actor),
			Flags:  uint32(result.flags),
		}
	} else {
		out = ActorLookupResult{Result: RequestInternalError}
	}
	state.result <- out
	if !state.once.closed {
		state.once.close(state.done)
	}
}
