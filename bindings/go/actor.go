// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "zlink.h"

extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkActorJoinTrampoline(zlink_actor_join_result_t *result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkActorLookupTrampoline(zlink_actor_lookup_result_t *result_, uintptr_t userdata_);
extern void goZlinkSpotActorLifecycleJoinTrampoline(void *spot_, zlink_spot_actor_lifecycle_info_t *info_, uintptr_t userdata_);
extern void goZlinkSpotActorLifecycleLeaveTrampoline(void *spot_, zlink_spot_actor_lifecycle_info_t *info_, uintptr_t userdata_);

static inline int zlink_spot_node_actor_destroy_close(void *node, const zlink_actor_ref_t *actor, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_destroy(node, actor, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, timeout_ms);
}

static inline int zlink_remote_actor_get_ref_go(void *node, const zlink_routing_id_t *target_node_rid, const char *actor_id, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_remote_actor_get_ref(node, target_node_rid, actor_id, (zlink_actor_lookup_handler_fn)goZlinkActorLookupTrampoline, (void *)userdata, timeout_ms);
}

static inline int zlink_spot_node_actor_join_spot_go_ops(void *node, const zlink_actor_ref_t *actor, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_join_spot(node, actor, dest_node_rid, dest_spot_rid, parts, part_count, (zlink_actor_join_handler_fn)goZlinkActorJoinTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_spot_node_actor_destroy_go_ops(void *node, const zlink_actor_ref_t *actor, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_destroy(node, actor, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, timeout_ms);
}

static inline int zlink_spot_node_actor_leave_spot_go_ops(void *node, const zlink_actor_ref_t *actor, const zlink_routing_id_t *spot, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_leave_spot(node, actor, spot, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, timeout_ms);
}

static inline int zlink_stream_bind_actor_go_ops(void *stream, const zlink_routing_id_t *session, const zlink_actor_ref_t *actor, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_stream_bind_actor(stream, session, actor, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, timeout_ms);
}

static inline int zlink_stream_unbind_actor_go_ops(void *stream, const zlink_routing_id_t *session, const char *actor_id, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_stream_unbind_actor(stream, session, actor_id, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, timeout_ms);
}

static inline int zlink_spot_actor_lifecycle_handler_go(void *spot, int with_join, int with_leave, uintptr_t userdata) {
    zlink_spot_actor_lifecycle_handler_fn join_fn = with_join ? (zlink_spot_actor_lifecycle_handler_fn)goZlinkSpotActorLifecycleJoinTrampoline : NULL;
    zlink_spot_actor_lifecycle_handler_fn leave_fn = with_leave ? (zlink_spot_actor_lifecycle_handler_fn)goZlinkSpotActorLifecycleLeaveTrampoline : NULL;
    return zlink_spot_actor_lifecycle_handler(spot, join_fn, leave_fn, (void *)userdata);
}
*/
import "C"

import (
	"runtime/cgo"
	"strings"
	"time"
	"unsafe"
)

const actorIDMax = int(C.ZLINK_ACTOR_ID_MAX)

// ActorRef identifies one logical actor. Generation==0 marks an unchecked
// remote ref.
type ActorRef struct {
	NodeRID    RoutingID
	ActorID    string
	Generation uint64
}

func (r ActorRef) IsUnchecked() bool {
	return r.Generation == 0
}

// ActorRoute is the resolution result from Discovery.ResolveActor.
type ActorRoute struct {
	Actor         ActorRef
	Joined        bool
	JoinedSpotRID *RoutingID
}

// ActorJoinRequest is the value yielded by Spot.RecvActorJoin. The Message
// carries the single join state payload.
type ActorJoinRequest struct {
	Info    ActorJoinInfo
	Message *Message
}

// ActorRecvInfo describes the routing context of a received actor part.
type ActorRecvInfo struct {
	Actor            ActorRef
	SourceNodeRID    RoutingID
	SourceSessionRID RoutingID
	Flags            uint32
}

// ActorJoinInfo captures both sides of an actor join exchange.
type ActorJoinInfo struct {
	SourceActor   ActorRef
	TargetActor   ActorRef
	SourceNodeRID RoutingID
	SourceSpotRID RoutingID
	TargetNodeRID RoutingID
	TargetSpotRID RoutingID
	JoinEpoch     uint64
	Flags         uint32
	raw           C.zlink_actor_join_info_t
}

// ActorPart is one part of an actor-routed multipart message.
type ActorPart struct {
	Info    ActorRecvInfo
	Message *Message
	More    bool
}

// SpotNodeSpotEntry is an entry from SpotNode.SpotsSnapshot.
type SpotNodeSpotEntry struct {
	SpotRID                 RoutingID
	DispatchHandlerAttached bool
	JoinedActorCount        uint32
	PendingActorJoinCount   uint32
	RouteSynced             bool
	LastChangedMs           uint64
}

// SpotNodeActorEntry is an entry from SpotNode.ActorsSnapshot.
type SpotNodeActorEntry struct {
	Actor               ActorRef
	Joined              bool
	JoinedSpotRID       RoutingID
	RouteSynced         bool
	PendingMessageCount uint32
	LastChangedMs       uint64
}

// Actor represents one local actor identity owned by a SpotNode.
type Actor struct {
	node   *SpotNode
	ref    ActorRef
	closed bool
}

// --- SpotNode actor methods ---

// Actor creates a local actor on this node.
func (n *SpotNode) Actor(actorID string) (*Actor, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	return withActorIDCString(actorID, func(actorIDC *C.char) (*Actor, error) {
		var raw C.zlink_actor_ref_t
		if err := configErrorFromResult(C.zlink_spot_node_actor_new(handle, actorIDC, &raw)); err != nil {
			return nil, err
		}
		return &Actor{node: n, ref: actorRefFromC(raw)}, nil
	})
}

// ActorLookup returns a checked ref for a local actor by id.
func (n *SpotNode) ActorLookup(actorID string) (ActorRef, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return ActorRef{}, err
	}
	return withActorIDCString(actorID, func(actorIDC *C.char) (ActorRef, error) {
		var raw C.zlink_actor_ref_t
		if err := configErrorFromResult(C.zlink_spot_node_actor_lookup(handle, actorIDC, &raw)); err != nil {
			return ActorRef{}, err
		}
		return actorRefFromC(raw), nil
	})
}

// RemoteActorRef builds an unchecked remote actor ref.
func RemoteActorRef(targetNodeRID RoutingID, actorID string) (ActorRef, error) {
	return ActorRef{NodeRID: targetNodeRID, ActorID: actorID}, nil
}

// RemoteActorGetRef returns an async lookup builder for a remote actor. The
// completion delivers ActorLookupResult with a checked ref on success.
func (n *SpotNode) RemoteActorGetRef(targetNodeRID RoutingID, actorID string) ActorLookupOp {
	return newActorLookupOp(func(timeout time.Duration, cb actorLookupCallback) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		target := targetNodeRID.toC()
		return withActorIDCStringErr(actorID, func(actorIDC *C.char) error {
			return submitActorLookupNative(nil, func(stateHandle cgo.Handle) error {
				return submitErrorFromResult(C.zlink_remote_actor_get_ref_go(handle, &target, actorIDC, C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
			}, cb)
		})
	})
}

// DestroyActor returns an async builder that destroys an Actor. Succeeds only
// when the Actor is in the Entry Spot.
func (n *SpotNode) DestroyActor(actor ActorRef) ActorDestroyOp {
	return newActorDestroyOp(nil, func(timeout time.Duration, cb requestPartsCallback) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := actor.toC()
		if err != nil {
			return err
		}
		return submitActorRequestNative(nil, func(stateHandle cgo.Handle) error {
			return submitErrorFromResult(C.zlink_spot_node_actor_destroy_go_ops(handle, &rawActor, C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
		}, cb)
	})
}

// JoinActor returns a user-Spot join builder. Completion delivers
// ActorJoinResult plus reply parts. destSpotRID must be a user Spot.
func (n *SpotNode) JoinActor(actor ActorRef, destNodeRID, destSpotRID RoutingID) ActorJoinOp {
	return newActorJoinOp(func(parts []*Message, flags SendFlags, timeout time.Duration, cb actorJoinCallback) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := actor.toC()
		if err != nil {
			return err
		}
		rawNode := destNodeRID.toC()
		rawSpot := destSpotRID.toC()
		return submitActorJoinNative(parts, nil, func(nativeParts *C.zlink_msg_t, partCount C.size_t, stateHandle cgo.Handle) error {
			return submitErrorFromResult(C.zlink_spot_node_actor_join_spot_go_ops(handle, &rawActor, &rawNode, &rawSpot, nativeParts, partCount, C.zlink_send_flags_t(flags), C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
		}, cb)
	})
}

// LeaveActor returns an async builder that moves an Actor to the same node's
// Entry Spot.
func (n *SpotNode) LeaveActor(actor ActorRef, currentSpotRID RoutingID) ActorLeaveOp {
	return newActorLeaveOp(func(timeout time.Duration, cb requestPartsCallback) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := actor.toC()
		if err != nil {
			return err
		}
		rawSpot := currentSpotRID.toC()
		return submitActorRequestNative(nil, func(stateHandle cgo.Handle) error {
			return submitErrorFromResult(C.zlink_spot_node_actor_leave_spot_go_ops(handle, &rawActor, &rawSpot, C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
		}, cb)
	})
}

// SendBoundSessionMsg returns an actor-to-session relay send builder. The
// underlying native call is synchronous; the builder model matches the rest
// of the spot send surface.
func (n *SpotNode) SendBoundSessionMsg(actor ActorRef) SendOp {
	return newSendBuilder(nil, func(parts []*Message, flags SendFlags) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := actor.toC()
		if err != nil {
			return err
		}
		return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			_ = partFlag
			return submitErrorFromResult(C.zlink_spot_node_actor_send_bound_session_msg(handle, &rawActor, part, C.zlink_send_flags_t(flags)))
		})
	})
}

// --- Actor methods ---

func (a *Actor) Ref() ActorRef {
	if a == nil || a.closed || a.node == nil {
		return ActorRef{}
	}
	return a.ref
}

// Join returns a user-Spot join builder bound to this Actor and the given
// destination Spot. The destination is resolved at submit time.
func (a *Actor) Join(spot *Spot) ActorJoinOp {
	return newActorJoinOp(func(parts []*Message, flags SendFlags, timeout time.Duration, cb actorJoinCallback) error {
		if a == nil || a.closed || a.node == nil || spot == nil || spot.core == nil || spot.core.closed {
			return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		nodeHandle, err := a.node.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := a.ref.toC()
		if err != nil {
			return err
		}
		destNode, err := a.node.RoutingID()
		if err != nil {
			return err
		}
		destSpot, err := spot.RoutingID()
		if err != nil {
			return err
		}
		rawNode := destNode.toC()
		rawSpot := destSpot.toC()
		return submitActorJoinNative(parts, spot.raw(), func(nativeParts *C.zlink_msg_t, partCount C.size_t, stateHandle cgo.Handle) error {
			return submitErrorFromResult(C.zlink_spot_node_actor_join_spot_go_ops(nodeHandle, &rawActor, &rawNode, &rawSpot, nativeParts, partCount, C.zlink_send_flags_t(flags), C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
		}, cb)
	})
}

// Leave returns an async builder that returns this Actor to its node's
// Entry Spot.
func (a *Actor) Leave(spot *Spot) ActorLeaveOp {
	return newActorLeaveOp(func(timeout time.Duration, cb requestPartsCallback) error {
		if a == nil || a.closed || a.node == nil || spot == nil || spot.core == nil || spot.core.closed {
			return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		nodeHandle, err := a.node.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := a.ref.toC()
		if err != nil {
			return err
		}
		destSpot, err := spot.RoutingID()
		if err != nil {
			return err
		}
		rawSpot := destSpot.toC()
		return submitActorRequestNative(nil, func(stateHandle cgo.Handle) error {
			return submitErrorFromResult(C.zlink_spot_node_actor_leave_spot_go_ops(nodeHandle, &rawActor, &rawSpot, C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
		}, cb)
	})
}

func (a *Actor) RecvPart(flags RecvFlags) (*ActorPart, error) {
	if a == nil || a.closed || a.node == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	node, err := a.node.handleOrError()
	if err != nil {
		return nil, err
	}
	return recvActorPart(node, a.ref, flags)
}

// SendBoundSession returns an actor-to-session relay send builder for the
// session that this Actor is currently bound to.
func (a *Actor) SendBoundSession() SendOp {
	return newSendBuilder(nil, func(parts []*Message, flags SendFlags) error {
		if a == nil || a.closed || a.node == nil {
			return &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		nodeHandle, err := a.node.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := a.ref.toC()
		if err != nil {
			return err
		}
		return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			_ = partFlag
			return submitErrorFromResult(C.zlink_spot_node_actor_send_bound_session_msg(nodeHandle, &rawActor, part, C.zlink_send_flags_t(flags)))
		})
	})
}

// CloseBoundSession closes the bound session of this Actor.
func (a *Actor) CloseBoundSession(timeout time.Duration) error {
	if a == nil || a.closed || a.node == nil {
		return &RequestError{Result: RequestTerminated, internalErrno: int(C.EFAULT)}
	}
	node, err := a.node.handleOrError()
	if err != nil {
		return err
	}
	rawActor, err := a.ref.toC()
	if err != nil {
		return err
	}
	timeoutMs := uint32(timeout.Milliseconds())
	return requestErrorFromResult(C.zlink_spot_node_actor_close_bound_session(node, &rawActor, C.uint32_t(timeoutMs)))
}

// CloseWithTimeout destroys the Actor synchronously with the given timeout.
func (a *Actor) CloseWithTimeout(timeout time.Duration) error {
	if a == nil || a.closed {
		return nil
	}
	node, err := a.node.handleOrError()
	if err != nil {
		return err
	}
	raw, err := a.ref.toC()
	if err != nil {
		return err
	}
	if err := waitActorReply(func(cb cgo.Handle) error {
		return submitErrorFromResult(C.zlink_spot_node_actor_destroy_close(node, &raw, C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(cb)))
	}); err != nil {
		return err
	}
	a.closed = true
	return nil
}

// Close destroys the Actor synchronously with the default request timeout.
func (a *Actor) Close() error {
	return a.CloseWithTimeout(0)
}

// --- Spot actor-side methods ---

// RecvActorJoin receives the next pending actor-join request on this Spot.
func (s *Spot) RecvActorJoin(flags RecvFlags) (*ActorJoinRequest, error) {
	if s == nil || s.core == nil || s.core.closed {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var rawInfo C.zlink_actor_join_info_t
	var parts *C.zlink_msg_t
	var partCount C.size_t
	if err := recvErrorFromResult(C.zlink_spot_actor_join_recv(s.raw(), &rawInfo, &parts, &partCount, C.zlink_recv_flags_t(flags))); err != nil {
		return nil, err
	}
	msg := &Message{}
	if err := configErrorFromResult(C.zlink_msg_init(&msg.msg)); err != nil {
		C.zlink_multipart_close(parts, partCount)
		return nil, err
	}
	if partCount > 0 {
		if err := configErrorFromResult(C.zlink_msg_move(&msg.msg, parts)); err != nil {
			_ = msg.Close()
			C.zlink_multipart_close(parts, partCount)
			return nil, err
		}
	}
	C.zlink_multipart_close(parts, partCount)
	info := actorJoinInfoFromC(rawInfo)
	return &ActorJoinRequest{Info: *info, Message: msg}, nil
}

// ReplyActorJoin returns a builder for the reply to a previously received
// actor-join request. accepted: true=accept, false=reject. The reply payload
// is optional.
func (s *Spot) ReplyActorJoin(request *ActorJoinRequest, accepted bool) ActorJoinReplyOp {
	return newActorJoinReplyOp(func(parts []*Message) error {
		if s == nil || s.core == nil || s.core.closed || request == nil {
			return &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		var accept C.uint32_t
		if accepted {
			accept = 1
		}
		if len(parts) == 0 {
			return submitErrorFromResult(C.zlink_spot_actor_join_reply(s.raw(), &request.Info.raw, accept, nil, 0))
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
		submitErr := submitErrorFromResult(C.zlink_spot_actor_join_reply(s.raw(), &request.Info.raw, accept, prepared.ptr(), prepared.count()))
		if submitErr != nil {
			_ = prepared.restore()
			return submitErr
		}
		prepared.commit()
		markPartsMoved(parts)
		return nil
	})
}

// OnActorLifecycle registers join/leave callbacks for this Spot. Passing nil
// for both removes the registration.
func (s *Spot) OnActorLifecycle(
	onJoin func(spot *Spot, info SpotActorLifecycleInfo),
	onLeave func(spot *Spot, info SpotActorLifecycleInfo)) error {
	if s == nil || s.core == nil || s.core.closed {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EFAULT)}
	}
	if onJoin == nil && onLeave == nil {
		// Pass NULL handlers to clear the registration.
		if err := handlerErrorFromResult(C.zlink_spot_actor_lifecycle_handler_go(s.raw(), 0, 0, 0)); err != nil {
			return err
		}
		if s.core.lifecycleHandle != 0 {
			releaseCallbackHandle(s.core.lifecycleHandle)
			s.core.lifecycleHandle = 0
		}
		return nil
	}
	state := newSpotActorLifecycleCallbackState(s, onJoin, onLeave)
	handle := cgo.NewHandle(state)
	withJoin := C.int(0)
	if onJoin != nil {
		withJoin = 1
	}
	withLeave := C.int(0)
	if onLeave != nil {
		withLeave = 1
	}
	if err := handlerErrorFromResult(C.zlink_spot_actor_lifecycle_handler_go(s.raw(), withJoin, withLeave, C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.core.lifecycleHandle != 0 {
		releaseCallbackHandle(s.core.lifecycleHandle)
	}
	s.core.lifecycleHandle = handle
	return nil
}

// ActorsSnapshot lists actors currently joined to this Spot.
func (s *Spot) ActorsSnapshot() ([]ActorRef, error) {
	if s == nil || s.core == nil || s.core.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var count C.size_t
	if err := configErrorFromResult(C.zlink_spot_actors_snapshot(s.raw(), nil, &count)); err != nil {
		return nil, err
	}
	if count == 0 {
		return []ActorRef{}, nil
	}
	entries := make([]C.zlink_actor_ref_t, int(count))
	if err := configErrorFromResult(C.zlink_spot_actors_snapshot(s.raw(), &entries[0], &count)); err != nil {
		return nil, err
	}
	out := make([]ActorRef, int(count))
	for i := range out {
		out[i] = actorRefFromC(entries[i])
	}
	return out, nil
}

// --- SpotNode snapshots ---

func (n *SpotNode) SpotsSnapshot() ([]SpotNodeSpotEntry, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	var count C.size_t
	if err := configErrorFromResult(C.zlink_spot_node_spots_snapshot(handle, nil, &count)); err != nil {
		return nil, err
	}
	if count == 0 {
		return []SpotNodeSpotEntry{}, nil
	}
	entries := make([]C.zlink_spot_node_spot_entry_t, int(count))
	if err := configErrorFromResult(C.zlink_spot_node_spots_snapshot(handle, &entries[0], &count)); err != nil {
		return nil, err
	}
	out := make([]SpotNodeSpotEntry, int(count))
	for i := range out {
		out[i] = spotNodeSpotEntryFromC(entries[i])
	}
	return out, nil
}

func (n *SpotNode) ActorsSnapshot() ([]SpotNodeActorEntry, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	var count C.size_t
	if err := configErrorFromResult(C.zlink_spot_node_actors_snapshot(handle, nil, &count)); err != nil {
		return nil, err
	}
	if count == 0 {
		return []SpotNodeActorEntry{}, nil
	}
	entries := make([]C.zlink_spot_node_actor_entry_t, int(count))
	if err := configErrorFromResult(C.zlink_spot_node_actors_snapshot(handle, &entries[0], &count)); err != nil {
		return nil, err
	}
	out := make([]SpotNodeActorEntry, int(count))
	for i := range out {
		out[i] = spotNodeActorEntryFromC(entries[i])
	}
	return out, nil
}

// --- Discovery ---

func (d *Discovery) ResolveActor(actorID string) (ActorRoute, error) {
	if d == nil || d.closed {
		return ActorRoute{}, stateError("discovery is closed")
	}
	return withActorIDCString(actorID, func(actorIDC *C.char) (ActorRoute, error) {
		var raw C.zlink_actor_route_t
		if err := configErrorFromResult(C.zlink_discovery_resolve_actor(d.raw(), actorIDC, &raw)); err != nil {
			return ActorRoute{}, err
		}
		return *actorRouteFromC(raw), nil
	})
}

// --- StreamSocket actor methods ---

// BindActor returns an Actor bind operation builder. The stream is bound to
// the given session_rid; the session does not need to be joined to a Spot.
func (s *StreamSocket) BindActor(sessionRID RoutingID, actor ActorRef) ActorBindOp {
	return newActorBindOp(func(timeout time.Duration, cb requestPartsCallback) error {
		if s == nil || s.core == nil || s.core.closed {
			return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		session := sessionRID.toC()
		rawActor, err := actor.toC()
		if err != nil {
			return err
		}
		return submitActorRequestNative(nil, func(stateHandle cgo.Handle) error {
			return submitErrorFromResult(C.zlink_stream_bind_actor_go_ops(s.raw(), &session, &rawActor, C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
		}, cb)
	})
}

// UnbindActor returns an Actor unbind operation builder for the given session
// and actor id.
func (s *StreamSocket) UnbindActor(sessionRID RoutingID, actorID string) ActorUnbindOp {
	return newActorUnbindOp(func(timeout time.Duration, cb requestPartsCallback) error {
		if s == nil || s.core == nil || s.core.closed {
			return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		session := sessionRID.toC()
		return withActorIDCStringErr(actorID, func(actorIDC *C.char) error {
			return submitActorRequestNative(nil, func(stateHandle cgo.Handle) error {
				return submitErrorFromResult(C.zlink_stream_unbind_actor_go_ops(s.raw(), &session, actorIDC, C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
			}, cb)
		})
	})
}

// SendBoundActor returns a session-bound relay send operation builder.
func (s *StreamSocket) SendBoundActor(sessionRID RoutingID, actorID string) SendOp {
	return newSendBuilder(nil, func(parts []*Message, flags SendFlags) error {
		if s == nil || s.core == nil || s.core.closed {
			return &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		session := sessionRID.toC()
		return withActorIDCStringErr(actorID, func(actorIDC *C.char) error {
			return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_stream_send_bound_actor_part(s.raw(), &session, actorIDC, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

// BoundActors returns the snapshot of Actor refs attached to the given session
// on this stream socket.
func (s *StreamSocket) BoundActors(sessionRID RoutingID) ([]ActorRef, error) {
	if s == nil || s.core == nil || s.core.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	session := sessionRID.toC()
	var count C.size_t
	if err := configErrorFromResult(C.zlink_stream_bound_actors(s.raw(), &session, nil, &count)); err != nil {
		return nil, err
	}
	if count == 0 {
		return []ActorRef{}, nil
	}
	entries := make([]C.zlink_actor_ref_t, int(count))
	if err := configErrorFromResult(C.zlink_stream_bound_actors(s.raw(), &session, &entries[0], &count)); err != nil {
		return nil, err
	}
	out := make([]ActorRef, int(count))
	for i := range out {
		out[i] = actorRefFromC(entries[i])
	}
	return out, nil
}

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
				Result:        resultCode,
				Actor:         actorRefFromC(result.actor),
				JoinedSpotRID: routingIDFromC(result.joined_spot_rid),
				JoinEpoch:     uint64(result.join_epoch),
				Flags:         uint32(result.flags),
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

//export goZlinkSpotActorLifecycleJoinTrampoline
func goZlinkSpotActorLifecycleJoinTrampoline(_ unsafe.Pointer, info *C.zlink_spot_actor_lifecycle_info_t, userdata C.uintptr_t) {
	dispatchSpotActorLifecycle(true, info, userdata)
}

//export goZlinkSpotActorLifecycleLeaveTrampoline
func goZlinkSpotActorLifecycleLeaveTrampoline(_ unsafe.Pointer, info *C.zlink_spot_actor_lifecycle_info_t, userdata C.uintptr_t) {
	dispatchSpotActorLifecycle(false, info, userdata)
}

func dispatchSpotActorLifecycle(isJoin bool, info *C.zlink_spot_actor_lifecycle_info_t, userdata C.uintptr_t) {
	if info == nil {
		return
	}
	state, ok := safeHandleAs[*spotActorLifecycleCallbackState](userdata)
	if !ok || state == nil {
		return
	}
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
	state.dispatch(isJoin, lifecycle)
}

func recvActorPart(node unsafe.Pointer, actor ActorRef, flags RecvFlags) (*ActorPart, error) {
	if node == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
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
		return zero, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
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
		return raw, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
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
		Actor:  actorRefFromC(raw.actor),
		Joined: raw.joined != 0,
	}
	if raw.joined != 0 {
		rid := routingIDFromC(raw.joined_spot_rid)
		route.JoinedSpotRID = &rid
	}
	return route
}

func spotNodeSpotEntryFromC(raw C.zlink_spot_node_spot_entry_t) SpotNodeSpotEntry {
	return SpotNodeSpotEntry{
		SpotRID:                 routingIDFromC(raw.spot_rid),
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
		Joined:              raw.joined != 0,
		JoinedSpotRID:       routingIDFromC(raw.joined_spot_rid),
		RouteSynced:         raw.route_synced != 0,
		PendingMessageCount: uint32(raw.pending_message_count),
		LastChangedMs:       uint64(raw.last_changed_ms),
	}
}
