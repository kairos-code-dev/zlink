// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "zlink.h"

extern int zlink_spot_request_progress_internal(void *spot_);
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
	Result        RequestResult
	Actor         ActorRef
	JoinedSpotRID RoutingID
	JoinEpoch     uint64
	Flags         uint32
}

// ActorLookupResult is the completion value of RemoteActorGetRef.
type ActorLookupResult struct {
	Result RequestResult
	Actor  ActorRef
	Flags  uint32
}

// SpotActorLifecycleInfo is the payload delivered to lifecycle callbacks.
type SpotActorLifecycleInfo struct {
	PreviousActor   ActorRef
	CurrentActor    ActorRef
	PreviousSpotRID *RoutingID
	CurrentSpotRID  *RoutingID
	JoinEpoch       uint64
	Flags           uint32
}

// --- builder interfaces ---

// ActorJoinOp is returned by SpotNode.JoinActor and Actor.Join. The first
// .Message(...) call advances to ActorJoinSubmitOp.
type ActorJoinOp interface {
	Message(message *Message) ActorJoinSubmitOp
}

// ActorJoinSubmitOp is the join operation builder after the first part is
// attached. Submit blocks until completion; Flags advances to the callback-only
// stage.
type ActorJoinSubmitOp interface {
	Message(message *Message) ActorJoinSubmitOp
	Timeout(timeout time.Duration) ActorJoinSubmitOp
	Flags(flags SendFlags) ActorJoinCallbackSubmitOp
	Submit(ctx context.Context) (ActorJoinResult, []*Message, error)
	SubmitCallback(ctx context.Context,
		callback func(result ActorJoinResult, parts []*Message)) (bool, error)
}

// ActorJoinCallbackSubmitOp is the callback-only stage of the join builder.
type ActorJoinCallbackSubmitOp interface {
	Message(message *Message) ActorJoinCallbackSubmitOp
	Timeout(timeout time.Duration) ActorJoinCallbackSubmitOp
	Flags(flags SendFlags) ActorJoinCallbackSubmitOp
	SubmitCallback(ctx context.Context,
		callback func(result ActorJoinResult, parts []*Message)) (bool, error)
}

// ActorJoinReplyOp is returned by Spot.ReplyActorJoin. The reply payload is
// optional; Submit may be called with zero attached parts.
type ActorJoinReplyOp interface {
	Message(message *Message) ActorJoinReplyOp
	Submit(ctx context.Context) error
}

// ActorLeaveOp is returned by SpotNode.LeaveActor / Actor.Leave.
type ActorLeaveOp interface {
	Timeout(timeout time.Duration) ActorLeaveOp
	Submit(ctx context.Context) ([]*Message, error)
	SubmitCallback(ctx context.Context,
		callback func(result RequestResult, parts []*Message)) (bool, error)
}

// ActorDestroyOp is returned by SpotNode.DestroyActor.
type ActorDestroyOp interface {
	Timeout(timeout time.Duration) ActorDestroyOp
	Submit(ctx context.Context) ([]*Message, error)
	SubmitCallback(ctx context.Context,
		callback func(result RequestResult, parts []*Message)) (bool, error)
}

// ActorLookupOp is returned by SpotNode.RemoteActorGetRef.
type ActorLookupOp interface {
	Timeout(timeout time.Duration) ActorLookupOp
	Submit(ctx context.Context) (ActorLookupResult, error)
	SubmitCallback(ctx context.Context,
		callback func(result ActorLookupResult)) (bool, error)
}

// ActorBindOp is returned by StreamSocket.BindActor.
type ActorBindOp interface {
	Timeout(timeout time.Duration) ActorBindOp
	Submit(ctx context.Context) ([]*Message, error)
	SubmitCallback(ctx context.Context,
		callback func(result RequestResult, parts []*Message)) (bool, error)
}

// ActorUnbindOp is returned by StreamSocket.UnbindActor.
type ActorUnbindOp interface {
	Timeout(timeout time.Duration) ActorUnbindOp
	Submit(ctx context.Context) ([]*Message, error)
	SubmitCallback(ctx context.Context,
		callback func(result RequestResult, parts []*Message)) (bool, error)
}

// --- common state ---

type actorJoinCallback func(result ActorJoinResult, parts []*Message)
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

func (b *actorJoinSubmitBuilder) Submit(_ context.Context) (ActorJoinResult, []*Message, error) {
	return b.state.doSubmitSync()
}

func (b *actorJoinSubmitBuilder) SubmitCallback(_ context.Context, callback func(ActorJoinResult, []*Message)) (bool, error) {
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

func (b *actorJoinCallbackBuilder) SubmitCallback(_ context.Context, callback func(ActorJoinResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (s *actorJoinBuilderState) doSubmitSync() (ActorJoinResult, []*Message, error) {
	if s.submitted {
		return ActorJoinResult{}, nil, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if len(s.parts) == 0 {
		return ActorJoinResult{}, nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	s.submitted = true
	resultCh := make(chan actorJoinTrampolineResult, 1)
	if err := s.submit(s.parts, s.flags, s.timeout, func(result ActorJoinResult, parts []*Message) {
		resultCh <- actorJoinTrampolineResult{result: result, parts: parts}
	}); err != nil {
		return ActorJoinResult{}, nil, err
	}
	out := <-resultCh
	if out.result.Result != RequestOK {
		return out.result, out.parts, &RequestError{Result: out.result.Result}
	}
	return out.result, out.parts, nil
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

func (s *requestPartsBuilderState) doSubmitSync() ([]*Message, error) {
	if s.submitted {
		return nil, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	s.submitted = true
	resultCh := make(chan requestResult, 1)
	if err := s.submit(s.timeout, func(r RequestResult, parts []*Message) {
		resultCh <- requestResult{result: r, parts: parts}
	}); err != nil {
		return nil, err
	}
	out := <-resultCh
	if out.result != RequestOK {
		closeMessageSlice(out.parts)
		return nil, &RequestError{Result: out.result}
	}
	return out.parts, nil
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

// actorLeaveBuilder

type actorLeaveBuilder struct {
	state *requestPartsBuilderState
}

func newActorLeaveOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorLeaveOp {
	return &actorLeaveBuilder{state: &requestPartsBuilderState{submit: submit}}
}

func (b *actorLeaveBuilder) Timeout(timeout time.Duration) ActorLeaveOp {
	b.state.timeout = timeout
	return b
}

func (b *actorLeaveBuilder) Submit(_ context.Context) ([]*Message, error) {
	return b.state.doSubmitSync()
}

func (b *actorLeaveBuilder) SubmitCallback(_ context.Context, cb func(RequestResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(cb)
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

func (b *actorDestroyBuilder) Submit(_ context.Context) ([]*Message, error) {
	parts, err := b.state.doSubmitSync()
	if err == nil && b.onSuccess != nil {
		b.onSuccess()
	}
	return parts, err
}

func (b *actorDestroyBuilder) SubmitCallback(_ context.Context, cb func(RequestResult, []*Message)) (bool, error) {
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

// actorBindBuilder

type actorBindBuilder struct {
	state *requestPartsBuilderState
}

func newActorBindOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorBindOp {
	return &actorBindBuilder{state: &requestPartsBuilderState{submit: submit}}
}

func (b *actorBindBuilder) Timeout(timeout time.Duration) ActorBindOp {
	b.state.timeout = timeout
	return b
}

func (b *actorBindBuilder) Submit(_ context.Context) ([]*Message, error) {
	return b.state.doSubmitSync()
}

func (b *actorBindBuilder) SubmitCallback(_ context.Context, cb func(RequestResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(cb)
}

// actorUnbindBuilder

type actorUnbindBuilder struct {
	state *requestPartsBuilderState
}

func newActorUnbindOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorUnbindOp {
	return &actorUnbindBuilder{state: &requestPartsBuilderState{submit: submit}}
}

func (b *actorUnbindBuilder) Timeout(timeout time.Duration) ActorUnbindOp {
	b.state.timeout = timeout
	return b
}

func (b *actorUnbindBuilder) Submit(_ context.Context) ([]*Message, error) {
	return b.state.doSubmitSync()
}

func (b *actorUnbindBuilder) SubmitCallback(_ context.Context, cb func(RequestResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(cb)
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

func (b *actorLookupBuilder) Submit(_ context.Context) (ActorLookupResult, error) {
	if b.state.submitted {
		return ActorLookupResult{}, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	b.state.submitted = true
	resultCh := make(chan ActorLookupResult, 1)
	if err := b.state.submit(b.state.timeout, func(r ActorLookupResult) {
		resultCh <- r
	}); err != nil {
		return ActorLookupResult{}, err
	}
	out := <-resultCh
	if out.Result != RequestOK {
		return out, &RequestError{Result: out.Result}
	}
	return out, nil
}

func (b *actorLookupBuilder) SubmitCallback(_ context.Context, cb func(ActorLookupResult)) (bool, error) {
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
	state := &replyCallbackState{
		result: make(chan requestResult, 1),
		done:   make(chan struct{}),
	}
	handle := cgo.NewHandle(state)
	state.metadata = &actorJoinMetadata{callback: callback}
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
		result := <-state.result
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
	state := &replyCallbackState{
		result: make(chan requestResult, 1),
		done:   make(chan struct{}),
	}
	handle := cgo.NewHandle(state)
	if err := native(handle); err != nil {
		handle.Delete()
		return err
	}
	if progressSpot != nil {
		startSpotRequestProgress(progressSpot, state)
	}
	go func() {
		result := <-state.result
		callback(result.result, result.parts)
	}()
	return nil
}

// submitActorLookupNative drives the actor lookup operation. The lookup
// trampoline carries the lookup result through a dedicated channel.
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
		go func() {
			ticker := time.NewTicker(time.Millisecond)
			defer ticker.Stop()
			for {
				select {
				case <-state.done:
					return
				default:
				}
				C.zlink_spot_request_progress_internal(progressSpot)
				select {
				case <-state.done:
					return
				case <-ticker.C:
				}
			}
		}()
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
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	defer handle.Delete()
	state, ok := value.(*actorLookupCallbackState)
	if !ok || state == nil {
		return
	}
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
