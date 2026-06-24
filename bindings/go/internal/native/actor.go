// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "zlink.h"

extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkActorJoinTrampoline(zlink_actor_join_result_t *result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkActorJoinEntrySpotTrampoline(zlink_actor_join_entry_spot_result_t *result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkActorLookupTrampoline(zlink_actor_lookup_result_t *result_, uintptr_t userdata_);

static inline int zlink_spot_node_actor_destroy_close(void *node, const zlink_actor_ref_t *actor, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_destroy(node, actor, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, timeout_ms);
}

static inline int zlink_remote_actor_get_ref_go(void *node, const zlink_routing_id_t *target_node_rid, const char *actor_id, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_remote_actor_get_ref(node, target_node_rid, actor_id, (zlink_actor_lookup_handler_fn)goZlinkActorLookupTrampoline, (void *)userdata, timeout_ms);
}

static inline int zlink_spot_node_actor_join_spot_go_ops(void *node, const zlink_actor_ref_t *actor, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_join_spot(node, actor, dest_node_rid, dest_spot_rid, parts, part_count, (zlink_actor_join_spot_handler_fn)goZlinkActorJoinTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_spot_node_actor_join_entry_spot_go_ops(void *node, const zlink_actor_ref_t *actor, const zlink_routing_id_t *dest_node_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_join_entry_spot(node, actor, dest_node_rid, parts, part_count, (zlink_actor_join_entry_spot_handler_fn)goZlinkActorJoinEntrySpotTrampoline, (void *)userdata, flags, timeout_ms);
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

*/
import "C"

import (
	"runtime/cgo"
	"time"
)

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

// JoinActorEntrySpot returns an Entry Spot join builder. Callers must pass the
// request message explicitly, using an empty Message when no payload is needed.
func (n *SpotNode) JoinActorEntrySpot(actor ActorRef, destNodeRID RoutingID, request *Message) ActorJoinEntrySpotOp {
	return newActorJoinEntrySpotOp(request, func(parts []*Message, flags SendFlags, timeout time.Duration, cb actorJoinEntrySpotCallback) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := actor.toC()
		if err != nil {
			return err
		}
		rawNode := destNodeRID.toC()
		return submitActorJoinEntrySpotNative(parts, func(nativeParts *C.zlink_msg_t, partCount C.size_t, stateHandle cgo.Handle) error {
			return submitErrorFromResult(C.zlink_spot_node_actor_join_entry_spot_go_ops(handle, &rawActor, &rawNode, nativeParts, partCount, C.zlink_send_flags_t(flags), C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(stateHandle)))
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
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := actor.toC()
		if err != nil {
			return err
		}
		return submitSingleFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
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
			return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
			return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
		return nil, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
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
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		if a == nil || a.closed || a.node == nil {
			return &SubmitError{Result: SubmitInvalidHandle, nativeErrno: int(C.EFAULT)}
		}
		nodeHandle, err := a.node.handleOrError()
		if err != nil {
			return err
		}
		rawActor, err := a.ref.toC()
		if err != nil {
			return err
		}
		return submitSingleFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			_ = partFlag
			return submitErrorFromResult(C.zlink_spot_node_actor_send_bound_session_msg(nodeHandle, &rawActor, part, C.zlink_send_flags_t(flags)))
		})
	})
}

// CloseBoundSession closes the bound session of this Actor.
func (a *Actor) CloseBoundSession(timeout time.Duration) error {
	if a == nil || a.closed || a.node == nil {
		return &RequestError{Result: RequestTerminated, nativeErrno: int(C.EFAULT)}
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
		return nil, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
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
// actor-join request. joinResultCode 0 accepts the join; non-zero rejects it
// with an application-defined code. The reply payload is optional.
func (s *Spot) ReplyActorJoin(request *ActorJoinRequest, joinResultCode int32) ActorJoinReplyOp {
	return newActorJoinReplyOp(func(parts []*Message) error {
		if s == nil || s.core == nil || s.core.closed || request == nil {
			return &SubmitError{Result: SubmitInvalidHandle, nativeErrno: int(C.EFAULT)}
		}
		code := C.int32_t(joinResultCode)
		if len(parts) == 0 {
			return submitErrorFromResult(C.zlink_spot_actor_join_reply(s.raw(), &request.Info.raw, code, nil, 0))
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
		submitErr := submitErrorFromResult(C.zlink_spot_actor_join_reply(s.raw(), &request.Info.raw, code, prepared.ptr(), prepared.count()))
		if submitErr != nil {
			_ = prepared.restore()
			return submitErr
		}
		prepared.commit()
		markPartsMoved(parts)
		return nil
	})
}

// Actors lists actors currently joined to this Spot.
func (s *Spot) Actors() ([]ActorRef, error) {
	if s == nil || s.core == nil || s.core.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	var count C.size_t
	if err := configErrorFromResult(C.zlink_spot_actors(s.raw(), nil, &count)); err != nil {
		return nil, err
	}
	if count == 0 {
		return []ActorRef{}, nil
	}
	entries := make([]C.zlink_actor_ref_t, int(count))
	if err := configErrorFromResult(C.zlink_spot_actors(s.raw(), &entries[0], &count)); err != nil {
		return nil, err
	}
	out := make([]ActorRef, int(count))
	for i := range out {
		out[i] = actorRefFromC(entries[i])
	}
	return out, nil
}

// --- SpotNode snapshots ---

func (n *SpotNode) Spots() ([]SpotNodeSpotEntry, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	var count C.size_t
	if err := configErrorFromResult(C.zlink_spot_node_spots(handle, nil, &count)); err != nil {
		return nil, err
	}
	if count == 0 {
		return []SpotNodeSpotEntry{}, nil
	}
	entries := make([]C.zlink_spot_node_spot_entry_t, int(count))
	if err := configErrorFromResult(C.zlink_spot_node_spots(handle, &entries[0], &count)); err != nil {
		return nil, err
	}
	out := make([]SpotNodeSpotEntry, int(count))
	for i := range out {
		out[i] = spotNodeSpotEntryFromC(entries[i])
	}
	return out, nil
}

func (n *SpotNode) Actors() ([]SpotNodeActorEntry, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	var count C.size_t
	if err := configErrorFromResult(C.zlink_spot_node_actors(handle, nil, &count)); err != nil {
		return nil, err
	}
	if count == 0 {
		return []SpotNodeActorEntry{}, nil
	}
	entries := make([]C.zlink_spot_node_actor_entry_t, int(count))
	if err := configErrorFromResult(C.zlink_spot_node_actors(handle, &entries[0], &count)); err != nil {
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
			return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
			return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		if s == nil || s.core == nil || s.core.closed {
			return &SubmitError{Result: SubmitInvalidHandle, nativeErrno: int(C.EFAULT)}
		}
		session := sessionRID.toC()
		return withActorIDCStringErr(actorID, func(actorIDC *C.char) error {
			return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_stream_send_bound_actor_part(s.raw(), &session, actorIDC, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

// BoundActors returns the snapshot of Actor refs attached to the given session
// on this stream socket.
func (s *StreamSocket) BoundActors(sessionRID RoutingID) ([]ActorRef, error) {
	if s == nil || s.core == nil || s.core.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
