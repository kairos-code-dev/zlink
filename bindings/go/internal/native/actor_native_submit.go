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
	"runtime/cgo"
	"unsafe"
)

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

func submitActorJoinEntrySpotNative(parts []*Message, native func(nativeParts *C.zlink_msg_t, partCount C.size_t, cb cgo.Handle) error, callback actorJoinEntrySpotCallback) error {
	cloned, err := cloneParts(parts)
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return err
	}
	state := &actorJoinEntrySpotCallbackState{
		result: make(chan actorJoinEntrySpotTrampolineResult, 1),
		done:   make(chan struct{}),
	}
	handle := cgo.NewHandle(state)
	if err := native(prepared.ptr(), prepared.count(), handle); err != nil {
		handle.Delete()
		_ = prepared.restore()
		return err
	}
	prepared.commit()
	markPartsMoved(parts)
	go func() {
		result := <-state.result
		callback(result.result, result.parts)
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
