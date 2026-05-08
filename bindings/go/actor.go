// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zlink.h"

extern zlink_actor_admission_result_t goZlinkActorAdmissionTrampoline(void *node_, const char *actor_id_, const zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_spot_node_actor_admission_handler_go(void *node, uintptr_t userdata) {
    return zlink_spot_node_actor_admission_handler(node, (zlink_actor_admission_handler_fn)goZlinkActorAdmissionTrampoline, (void *)userdata);
}

static inline int zlink_spot_node_actor_join_spot_go(void *node, const zlink_actor_ref_t *actor, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_join_spot(node, actor, dest_node_rid, dest_spot_rid, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
}
*/
import "C"

import (
	"errors"
	"runtime/cgo"
	"strings"
	"time"
	"unsafe"
)

const actorIDMax = int(C.ZLINK_ACTOR_ID_MAX)

type ActorRef struct {
	NodeRID    RoutingID
	ActorID    string
	Generation uint64
}

func (r ActorRef) IsUnchecked() bool {
	return r.Generation == 0
}

type ActorCreateStatus int

const (
	ActorCreateCreated  ActorCreateStatus = ActorCreateStatus(C.ZLINK_ACTOR_CREATE_CREATED)
	ActorCreateExisting ActorCreateStatus = ActorCreateStatus(C.ZLINK_ACTOR_CREATE_EXISTING)
)

type ActorAdmissionResult int

const (
	ActorAdmissionAccept ActorAdmissionResult = ActorAdmissionResult(C.ZLINK_ACTOR_ADMISSION_ACCEPT)
	ActorAdmissionReject ActorAdmissionResult = ActorAdmissionResult(C.ZLINK_ACTOR_ADMISSION_REJECT)
)

type ActorCreateResult struct {
	Status ActorCreateStatus
	Actor  ActorRef
}

type ActorRoute struct {
	Actor         ActorRef
	Joined        bool
	JoinedSpotRID *RoutingID
}

type ActorJoinRequest struct {
	Info    ActorJoinInfo
	Message *Message
}

type ActorRecvInfo struct {
	Actor            ActorRef
	SourceNodeRID    RoutingID
	SourceSessionRID RoutingID
	Flags            uint32
}

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

type ActorPart struct {
	Info    ActorRecvInfo
	Message *Message
	More    bool
}

type SpotNodeSpotEntry struct {
	SpotRID                 RoutingID
	DispatchHandlerAttached bool
	JoinedActorCount        uint32
	PendingActorJoinCount   uint32
	RouteSynced             bool
	LastChangedMs           uint64
}

type SpotNodeActorEntry struct {
	Actor               ActorRef
	Joined              bool
	JoinedSpotRID       RoutingID
	RouteSynced         bool
	PendingMessageCount uint32
	LastChangedMs       uint64
}

type Actor struct {
	node   *SpotNode
	ref    ActorRef
	closed bool
}

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

func RemoteActorRef(targetNodeRID RoutingID, actorID string) (ActorRef, error) {
	node := targetNodeRID.toC()
	return withActorIDCString(actorID, func(actorIDC *C.char) (ActorRef, error) {
		var raw C.zlink_actor_ref_t
		if err := configErrorFromResult(C.zlink_remote_actor_get_ref(&node, actorIDC, &raw)); err != nil {
			return ActorRef{}, err
		}
		return actorRefFromC(raw), nil
	})
}

func (n *SpotNode) CreateRemoteActor(targetNodeRID RoutingID, actorID string, message *Message, timeout time.Duration) (ActorCreateResult, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return ActorCreateResult{}, err
	}
	if message == nil || message.closed {
		return ActorCreateResult{}, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	node := targetNodeRID.toC()
	return withActorIDCString(actorID, func(actorIDC *C.char) (ActorCreateResult, error) {
		cloned, err := message.clone()
		if err != nil {
			return ActorCreateResult{}, err
		}
		prepared, err := prepareMultipart([]*Message{cloned})
		if err != nil {
			_ = cloned.Close()
			return ActorCreateResult{}, err
		}
		var raw C.zlink_actor_create_result_t
		rc := C.zlink_spot_node_create_remote_actor(handle, &node, actorIDC, prepared.ptr(), prepared.count(), &raw, C.uint32_t(requestTimeoutMillis(timeout)))
		if err := requestErrorFromResult(rc); err != nil {
			_ = prepared.restore()
			return ActorCreateResult{}, err
		}
		prepared.commit()
		message.moved()
		return ActorCreateResult{Status: ActorCreateStatus(raw.status), Actor: actorRefFromC(raw.actor)}, nil
	})
}

func (n *SpotNode) DestroyActor(actor ActorRef, timeout time.Duration) error {
	return n.DestroyRemoteActor(actor, timeout)
}

func (n *SpotNode) DestroyRemoteActor(actor ActorRef, timeout time.Duration) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	raw, err := actor.toC()
	if err != nil {
		return err
	}
	return requestErrorFromResult(C.zlink_spot_node_actor_destroy(handle, &raw, C.uint32_t(requestTimeoutMillis(timeout))))
}

func (n *SpotNode) OnActorAdmission(handler func(string, *Message) ActorAdmissionResult) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	state := newActorAdmissionCallbackState(handler)
	cb := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_spot_node_actor_admission_handler_go(handle, C.uintptr_t(cb))); err != nil {
		cb.Delete()
		return err
	}
	n.mu.Lock()
	old := n.admissionHandle
	n.admissionHandle = cb
	n.mu.Unlock()
	if old != 0 {
		releaseCallbackHandle(old)
	}
	return nil
}

func (n *SpotNode) JoinActor(actor ActorRef, destNodeRID RoutingID, destSpotRID RoutingID, message *Message, callback RequestReplyCallback, flags SendFlags, timeout time.Duration) (bool, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return false, err
	}
	if callback == nil || message == nil || message.closed {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	rawActor, err := actor.toC()
	if err != nil {
		return false, err
	}
	rawSpot := destSpotRID.toC()
	rawNode := destNodeRID.toC()
	if err := submitActorJoin(message, nil, func(part *C.zlink_msg_t, cb cgo.Handle) error {
		return submitErrorFromResult(C.zlink_spot_node_actor_join_spot_go(handle, &rawActor, &rawNode, &rawSpot, part, 1, C.zlink_send_flags_t(flags), C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(cb)))
	}, callback); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

func (n *SpotNode) LeaveActor(actor ActorRef, destSpotRID RoutingID, timeout time.Duration) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	rawActor, err := actor.toC()
	if err != nil {
		return err
	}
	rawSpot := destSpotRID.toC()
	return requestErrorFromResult(C.zlink_spot_node_actor_leave_spot(handle, &rawActor, &rawSpot, C.uint32_t(requestTimeoutMillis(timeout))))
}

func (a *Actor) Ref() ActorRef {
	if a == nil || a.closed || a.node == nil {
		return ActorRef{}
	}
	return a.ref
}

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
	if err := requestErrorFromResult(C.zlink_spot_node_actor_destroy(node, &raw, C.uint32_t(requestTimeoutMillis(timeout)))); err != nil {
		return err
	}
	a.closed = true
	return nil
}

func (a *Actor) Close() error {
	return a.CloseWithTimeout(0)
}

func (a *Actor) Join(spot *Spot, message *Message, callback RequestReplyCallback, flags SendFlags, timeout time.Duration) (bool, error) {
	if a == nil || a.closed || a.node == nil || spot == nil || spot.core == nil || spot.core.closed {
		return false, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if callback == nil || message == nil || message.closed {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	node, err := a.node.handleOrError()
	if err != nil {
		return false, err
	}
	rawActor, err := a.ref.toC()
	if err != nil {
		return false, err
	}
	destNode, err := a.node.RoutingID()
	if err != nil {
		return false, err
	}
	destSpot, err := spot.RoutingID()
	if err != nil {
		return false, err
	}
	rawNode := destNode.toC()
	rawSpot := destSpot.toC()
	if err := submitActorJoin(message, spot.raw(), func(part *C.zlink_msg_t, cb cgo.Handle) error {
		return submitErrorFromResult(C.zlink_spot_node_actor_join_spot_go(node, &rawActor, &rawNode, &rawSpot, part, 1, C.zlink_send_flags_t(flags), C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(cb)))
	}, callback); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

func (a *Actor) Leave(spot *Spot, timeout time.Duration) error {
	if a == nil || a.closed || a.node == nil || spot == nil || spot.core == nil || spot.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	node, err := a.node.handleOrError()
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
	return requestErrorFromResult(C.zlink_spot_node_actor_leave_spot(node, &rawActor, &rawSpot, C.uint32_t(requestTimeoutMillis(timeout))))
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

func (a *Actor) SendBoundSession(message *Message, flags SendFlags) (bool, error) {
	if a == nil || a.closed || a.node == nil || message == nil || message.closed {
		return false, &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	node, err := a.node.handleOrError()
	if err != nil {
		return false, err
	}
	rawActor, err := a.ref.toC()
	if err != nil {
		return false, err
	}
	cloned, err := message.clone()
	if err != nil {
		return false, err
	}
	prepared, err := prepareMultipart([]*Message{cloned})
	if err != nil {
		_ = cloned.Close()
		return false, err
	}
	submitErr := submitErrorFromResult(C.zlink_spot_node_actor_send_bound_session_msg(node, &rawActor, prepared.ptr(), C.zlink_send_flags_t(flags)))
	if submitErr != nil {
		_ = prepared.restore()
		var se *SubmitError
		if errors.As(submitErr, &se) && se.Result == SubmitBackpressured {
			return false, nil
		}
		return false, submitErr
	}
	prepared.commit()
	message.moved()
	return true, nil
}

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

func (s *Spot) ReplyActorJoin(request *ActorJoinRequest, result ActorAdmissionResult, flags SendFlags, message *Message) (bool, error) {
	if s == nil || s.core == nil || s.core.closed || request == nil || message == nil || message.closed {
		return false, &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	_ = flags // flags not used in C API
	cloned, err := message.clone()
	if err != nil {
		return false, err
	}
	prepared, err := prepareMultipart([]*Message{cloned})
	if err != nil {
		_ = cloned.Close()
		return false, err
	}
	var accept C.uint32_t
	if result == ActorAdmissionAccept {
		accept = 1
	}
	submitErr := submitErrorFromResult(C.zlink_spot_actor_join_reply(s.raw(), &request.Info.raw, accept, prepared.ptr(), prepared.count()))
	if submitErr != nil {
		_ = prepared.restore()
		var se *SubmitError
		if errors.As(submitErr, &se) && se.Result == SubmitBackpressured {
			return false, nil
		}
		return false, submitErr
	}
	prepared.commit()
	message.moved()
	return true, nil
}

func (s *Spot) ActorsSnapshot() ([]ActorRef, error) {
	if s == nil || s.core == nil || s.core.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var count C.size_t
	if err := configErrorFromResult(C.zlink_spot_actors_snapshot(s.raw(), nil, &count)); err != nil {
		return nil, err
	}
	entries := make([]C.zlink_actor_ref_t, int(count))
	if count == 0 {
		return []ActorRef{}, nil
	}
	if err := configErrorFromResult(C.zlink_spot_actors_snapshot(s.raw(), &entries[0], &count)); err != nil {
		return nil, err
	}
	out := make([]ActorRef, int(count))
	for i := range out {
		out[i] = actorRefFromC(entries[i])
	}
	return out, nil
}

func (n *SpotNode) SpotsSnapshot() ([]SpotNodeSpotEntry, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	var count C.size_t
	if err := configErrorFromResult(C.zlink_spot_node_spots_snapshot(handle, nil, &count)); err != nil {
		return nil, err
	}
	entries := make([]C.zlink_spot_node_spot_entry_t, int(count))
	if count == 0 {
		return []SpotNodeSpotEntry{}, nil
	}
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
	entries := make([]C.zlink_spot_node_actor_entry_t, int(count))
	if count == 0 {
		return []SpotNodeActorEntry{}, nil
	}
	if err := configErrorFromResult(C.zlink_spot_node_actors_snapshot(handle, &entries[0], &count)); err != nil {
		return nil, err
	}
	out := make([]SpotNodeActorEntry, int(count))
	for i := range out {
		out[i] = spotNodeActorEntryFromC(entries[i])
	}
	return out, nil
}

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

func (s *StreamSocket) BindActor(node *SpotNode, sessionRID RoutingID, actor ActorRef, timeout time.Duration) error {
	if node == nil || s == nil || s.core == nil || s.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	nodeHandle, err := node.handleOrError()
	if err != nil {
		return err
	}
	session := sessionRID.toC()
	rawActor, err := actor.toC()
	if err != nil {
		return err
	}
	if err := requestErrorFromResult(C.zlink_stream_bind_actor(nodeHandle, s.raw(), &session, &rawActor, C.uint32_t(requestTimeoutMillis(timeout)))); err != nil {
		return err
	}
	s.boundSessions.Store(sessionRID.Hex(), actor.ActorID)
	return nil
}

func (s *StreamSocket) UnbindActor(node *SpotNode, sessionRID RoutingID, timeout time.Duration) error {
	if node == nil || s == nil || s.core == nil || s.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	nodeHandle, err := node.handleOrError()
	if err != nil {
		return err
	}
	key := sessionRID.Hex()
	actorIDVal, ok := s.boundSessions.Load(key)
	if !ok {
		return &ConfigError{Result: ConfigNotFound, internalErrno: int(C.ENOENT)}
	}
	actorID := actorIDVal.(string)
	session := sessionRID.toC()
	if err := withActorIDCStringErr(actorID, func(actorIDC *C.char) error {
		return requestErrorFromResult(C.zlink_stream_unbind_actor(nodeHandle, s.raw(), &session, actorIDC, C.uint32_t(requestTimeoutMillis(timeout))))
	}); err != nil {
		return err
	}
	s.boundSessions.Delete(key)
	return nil
}

func (s *StreamSocket) SendBoundActor(node *SpotNode, sessionRID RoutingID, message *Message, flags SendFlags) (bool, error) {
	if node == nil || s == nil || s.core == nil || s.core.closed {
		return false, &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if message == nil || message.closed {
		return false, &SubmitError{Result: SubmitInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	nodeHandle, err := node.handleOrError()
	if err != nil {
		return false, err
	}
	key := sessionRID.Hex()
	actorIDVal, ok := s.boundSessions.Load(key)
	if !ok {
		return false, &SubmitError{Result: SubmitNotFound, internalErrno: int(C.ENOENT)}
	}
	actorID := actorIDVal.(string)
	session := sessionRID.toC()
	submitErr := withActorIDCStringErr(actorID, func(actorIDC *C.char) error {
		return submitMultipartFromClones([]*Message{message}, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_stream_send_bound_actor_part(nodeHandle, s.raw(), &session, actorIDC, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
	if submitErr != nil {
		var se *SubmitError
		if errors.As(submitErr, &se) && se.Result == SubmitBackpressured {
			return false, nil
		}
		return false, submitErr
	}
	return true, nil
}

func submitActorJoin(message *Message, progressSpot unsafe.Pointer, submit func(*C.zlink_msg_t, cgo.Handle) error, callback RequestReplyCallback) error {
	cloned, err := message.clone()
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart([]*Message{cloned})
	if err != nil {
		_ = cloned.Close()
		return err
	}
	state := &replyCallbackState{
		result: make(chan requestResult, 1),
		done:   make(chan struct{}),
	}
	handle := cgo.NewHandle(state)
	if err := submit(prepared.ptr(), handle); err != nil {
		handle.Delete()
		_ = prepared.restore()
		return err
	}
	prepared.commit()
	message.moved()
	if progressSpot != nil {
		startSpotRequestProgress(progressSpot, state)
	}
	go func() {
		result := <-state.result
		callback(result.result, result.parts)
	}()
	return nil
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
