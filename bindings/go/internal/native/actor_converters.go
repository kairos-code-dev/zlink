// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "zlink.h"
*/
import "C"

import (
	"runtime/cgo"
	"strings"
	"unsafe"
)

// --- internal helpers ---

func waitActorReply(submit func(cgo.Handle) error) error {
	state := newReplyCallbackState()
	handle := cgo.NewHandle(state)
	if err := submit(handle); err != nil {
		handle.Delete()
		return err
	}
	result := state.wait()
	if result.result != RequestOK {
		return requestErrorFromResult(C.zlink_request_result_t(result.result))
	}
	closeMessageSlice(result.parts)
	return nil
}

//export goZlinkActorJoinTrampoline
func goZlinkActorJoinTrampoline(result *C.zlink_actor_join_result_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	handle := cgo.Handle(userdata)
	state, ok := safeHandleAs[*replyCallbackState](userdata)
	if !ok || state == nil {
		return
	}
	defer handle.Delete()
	var resultCode RequestResult
	if result == nil {
		resultCode = RequestInternalError
	} else {
		resultCode = RequestResult(result.result)
		if meta, ok := state.metadata.(*actorJoinMetadata); ok && meta != nil {
			meta.joinResult = ActorJoinResult{
				Result:         resultCode,
				JoinResultCode: int32(result.join_result_code),
				Actor:          actorRefFromC(result.actor),
				JoinedSpotRID:  routingIDFromC(result.joined_spot_rid),
				JoinEpoch:      uint64(result.join_epoch),
				Flags:          uint32(result.flags),
			}
		}
	}
	if resultCode == RequestOK {
		clonedParts, err := takeParts(parts, partCount)
		if err != nil {
			state.complete(requestResult{result: RequestProtocolError})
			return
		}
		state.complete(requestResult{result: RequestOK, parts: clonedParts})
		return
	}
	state.complete(requestResult{result: resultCode, parts: nil})
}

//export goZlinkActorJoinEntrySpotTrampoline
func goZlinkActorJoinEntrySpotTrampoline(result *C.zlink_actor_join_entry_spot_result_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	handle := cgo.Handle(userdata)
	state, ok := safeHandleAs[*actorJoinEntrySpotCallbackState](userdata)
	if !ok || state == nil {
		if parts != nil {
			C.zlink_multipart_close(parts, partCount)
		}
		return
	}
	defer handle.Delete()
	var out ActorJoinEntrySpotResult
	if result != nil {
		out = ActorJoinEntrySpotResult{
			Result:         RequestResult(result.result),
			JoinResultCode: int32(result.join_result_code),
			Actor:          actorRefFromC(result.actor),
			TargetNodeRID:  routingIDFromC(result.target_node_rid),
			JoinedSpotRID:  routingIDFromC(result.joined_spot_rid),
			JoinEpoch:      uint64(result.join_epoch),
			Flags:          uint32(result.flags),
		}
	} else {
		out = ActorJoinEntrySpotResult{Result: RequestInternalError}
	}
	if out.Result == RequestOK {
		clonedParts, err := takeParts(parts, partCount)
		if err != nil {
			state.result <- actorJoinEntrySpotTrampolineResult{
				result: ActorJoinEntrySpotResult{Result: RequestProtocolError},
			}
		} else {
			state.result <- actorJoinEntrySpotTrampolineResult{result: out, parts: clonedParts}
		}
	} else {
		if parts != nil {
			C.zlink_multipart_close(parts, partCount)
		}
		state.result <- actorJoinEntrySpotTrampolineResult{result: out}
	}
	if !state.once.closed {
		state.once.close(state.done)
	}
}

func spotActorLifecycleInfoFromC(info *C.zlink_spot_actor_lifecycle_info_t) SpotActorLifecycleInfo {
	lifecycle := SpotActorLifecycleInfo{
		PreviousActor: actorRefFromC(info.previous_actor),
		CurrentActor:  actorRefFromC(info.current_actor),
		JoinEpoch:     uint64(info.join_epoch),
		Flags:         uint32(info.flags),
	}
	prevSpot := routingIDFromC(info.previous_spot_rid)
	if prevSpot.Size() > 0 {
		lifecycle.PreviousSpotRID = &prevSpot
	}
	currSpot := routingIDFromC(info.current_spot_rid)
	if currSpot.Size() > 0 {
		lifecycle.CurrentSpotRID = &currSpot
	}
	return lifecycle
}

func recvActorPart(node unsafe.Pointer, actor ActorRef, flags RecvFlags) (*ActorPart, error) {
	if node == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	rawActor, err := actor.toC()
	if err != nil {
		return nil, err
	}
	var info C.zlink_actor_recv_info_t
	var part C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
		return nil, err
	}
	var more C.zlink_part_flag_t
	if err := recvErrorFromResult(C.zlink_spot_node_actor_recv_part(node, &rawActor, &info, &part, &more, C.zlink_recv_flags_t(flags))); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&part))
		return nil, err
	}
	msg := &Message{}
	if err := configErrorFromResult(C.zlink_msg_init(&msg.msg)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&part))
		return nil, err
	}
	if err := configErrorFromResult(C.zlink_msg_move(&msg.msg, &part)); err != nil {
		_ = msg.Close()
		_ = configErrorFromResult(C.zlink_msg_close(&part))
		return nil, err
	}
	return &ActorPart{Info: actorRecvInfoFromC(info), Message: msg, More: more != C.ZLINK_PART_FINAL}, nil
}

func withActorIDCString[T any](actorID string, fn func(*C.char) (T, error)) (T, error) {
	var zero T
	if actorID == "" || len(actorID) >= actorIDMax || strings.IndexByte(actorID, 0) >= 0 {
		return zero, &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	cstr := C.CString(actorID)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func withActorIDCStringErr(actorID string, fn func(*C.char) error) error {
	_, err := withActorIDCString(actorID, func(actorIDC *C.char) (struct{}, error) {
		return struct{}{}, fn(actorIDC)
	})
	return err
}

func actorRefFromC(raw C.zlink_actor_ref_t) ActorRef {
	return ActorRef{
		NodeRID:    routingIDFromC(raw.node_rid),
		ActorID:    C.GoString(&raw.actor_id[0]),
		Generation: uint64(raw.generation),
	}
}

func (r ActorRef) toC() (C.zlink_actor_ref_t, error) {
	var raw C.zlink_actor_ref_t
	raw.node_rid = r.NodeRID.toC()
	raw.generation = C.uint64_t(r.Generation)
	if r.ActorID == "" || len(r.ActorID) >= actorIDMax || strings.IndexByte(r.ActorID, 0) >= 0 {
		return raw, &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	cstr := C.CString(r.ActorID)
	defer C.free(unsafe.Pointer(cstr))
	C.strncpy(&raw.actor_id[0], cstr, C.size_t(actorIDMax-1))
	return raw, nil
}

func actorRecvInfoFromC(raw C.zlink_actor_recv_info_t) ActorRecvInfo {
	return ActorRecvInfo{
		Actor:            actorRefFromC(raw.actor),
		SourceNodeRID:    routingIDFromC(raw.source_node_rid),
		SourceSessionRID: routingIDFromC(raw.source_session_rid),
		Flags:            uint32(raw.flags),
	}
}

func actorJoinInfoFromC(raw C.zlink_actor_join_info_t) *ActorJoinInfo {
	return &ActorJoinInfo{
		SourceActor:   actorRefFromC(raw.source_actor),
		TargetActor:   actorRefFromC(raw.target_actor),
		SourceNodeRID: routingIDFromC(raw.source_node_rid),
		SourceSpotRID: routingIDFromC(raw.source_spot_rid),
		TargetNodeRID: routingIDFromC(raw.target_node_rid),
		TargetSpotRID: routingIDFromC(raw.target_spot_rid),
		JoinEpoch:     uint64(raw.join_epoch),
		Flags:         uint32(raw.flags),
		raw:           raw,
	}
}

func actorRouteFromC(raw C.zlink_actor_route_t) *ActorRoute {
	route := &ActorRoute{
		Actor:           actorRefFromC(raw.actor),
		CurrentSpotRID:  routingIDFromC(raw.current_spot_rid),
		CurrentSpotKind: SpotKind(raw.current_spot_kind),
	}
	return route
}

func spotNodeSpotEntryFromC(raw C.zlink_spot_node_spot_entry_t) SpotNodeSpotEntry {
	return SpotNodeSpotEntry{
		SpotRID:                 routingIDFromC(raw.spot_rid),
		SpotKind:                SpotKind(raw.spot_kind),
		DispatchHandlerAttached: raw.dispatch_handler_attached != 0,
		JoinedActorCount:        uint32(raw.joined_actor_count),
		PendingActorJoinCount:   uint32(raw.pending_actor_join_count),
		RouteSynced:             raw.route_synced != 0,
		LastChangedMs:           uint64(raw.last_changed_ms),
	}
}

func spotNodeActorEntryFromC(raw C.zlink_spot_node_actor_entry_t) SpotNodeActorEntry {
	return SpotNodeActorEntry{
		Actor:               actorRefFromC(raw.actor),
		CurrentSpotRID:      routingIDFromC(raw.current_spot_rid),
		CurrentSpotKind:     SpotKind(raw.current_spot_kind),
		RouteSynced:         raw.route_synced != 0,
		PendingMessageCount: uint32(raw.pending_message_count),
		LastChangedMs:       uint64(raw.last_changed_ms),
	}
}
