// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zlink.h"

extern zlink_actor_admission_result_t goZlinkActorAdmissionTrampoline(void *node_, const char *actor_id_, const zlink_msg_t *message_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_spot_node_actor_admission_handler_go(void *node, uintptr_t userdata) {
    return zlink_spot_node_actor_admission_handler(node, (zlink_actor_admission_handler_fn)goZlinkActorAdmissionTrampoline, (void *)userdata);
}

static inline int zlink_actor_join_spot_go(void *actor, void *spot, zlink_msg_t *message, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_actor_join_spot(actor, spot, message, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_spot_node_actor_join_spot_go(void *node, const zlink_actor_ref_t *actor, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *message, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_node_actor_join_spot(node, actor, dest_spot_rid, message, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
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
	JoinedSpotRID RoutingID
}

type ActorRecvInfo struct {
	Actor            ActorRef
	SourceNodeRID    RoutingID
	SourceSessionRID RoutingID
	Flags            uint32
}

type ActorJoinInfo struct {
	Actor         ActorRef
	SourceNodeRID RoutingID
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
	handle unsafe.Pointer
	closed bool
}

func (n *SpotNode) Actor(actorID string) (*Actor, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	return withActorIDCString(actorID, func(actorIDC *C.char) (*Actor, error) {
		actor := C.zlink_spot_node_actor_new(handle, actorIDC)
		if actor == nil {
			return nil, configErrorFromErrno(currentErrno())
		}
		return &Actor{handle: actor}, nil
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

func (n *SpotNode) CreateRemoteActor(targetNodeRID RoutingID, actorID string, message *Message, timeout time.Duration) (*ActorCreateResult, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	if message == nil || message.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	node := targetNodeRID.toC()
	return withActorIDCString(actorID, func(actorIDC *C.char) (*ActorCreateResult, error) {
		cloned, err := message.clone()
		if err != nil {
			return nil, err
		}
		prepared, err := prepareMultipart([]*Message{cloned})
		if err != nil {
			_ = cloned.Close()
			return nil, err
		}
		var raw C.zlink_actor_create_result_t
		rc := C.zlink_spot_node_create_remote_actor(handle, &node, actorIDC, prepared.ptr(), &raw, C.uint32_t(requestTimeoutMillis(timeout)))
		if err := requestErrorFromResult(rc); err != nil {
			_ = prepared.restore()
			return nil, err
		}
		prepared.commit()
		message.moved()
		return &ActorCreateResult{Status: ActorCreateStatus(raw.status), Actor: actorRefFromC(raw.actor)}, nil
	})
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
	return requestErrorFromResult(C.zlink_spot_node_destroy_remote_actor(handle, &raw, C.uint32_t(requestTimeoutMillis(timeout))))
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

func (n *SpotNode) JoinActor(actor ActorRef, destSpotRID RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, message *Message) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	if callback == nil || message == nil || message.closed {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	rawActor, err := actor.toC()
	if err != nil {
		return err
	}
	rawSpot := destSpotRID.toC()
	return submitActorJoin(message, nil, func(part *C.zlink_msg_t, cb cgo.Handle) error {
		return submitErrorFromResult(C.zlink_spot_node_actor_join_spot_go(handle, &rawActor, &rawSpot, part, C.zlink_send_flags_t(flags), C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(cb)))
	}, callback)
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

func (a *Actor) Ref() (ActorRef, error) {
	if a == nil || a.closed || a.handle == nil {
		return ActorRef{}, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var raw C.zlink_actor_ref_t
	if err := configErrorFromResult(C.zlink_actor_get_ref(a.handle, &raw)); err != nil {
		return ActorRef{}, err
	}
	return actorRefFromC(raw), nil
}

func (a *Actor) CloseWithTimeout(timeout time.Duration) error {
	if a == nil || a.closed {
		return nil
	}
	handle := a.handle
	if err := requestErrorFromResult(C.zlink_actor_destroy(&handle, C.uint32_t(requestTimeoutMillis(timeout)))); err != nil {
		return err
	}
	a.closed = true
	a.handle = nil
	return nil
}

func (a *Actor) Close() error {
	return a.CloseWithTimeout(0)
}

func (a *Actor) Join(spot *Spot, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, message *Message) error {
	if a == nil || a.closed || a.handle == nil || spot == nil || spot.core == nil || spot.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if callback == nil || message == nil || message.closed {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	return submitActorJoin(message, spot.raw(), func(part *C.zlink_msg_t, cb cgo.Handle) error {
		return submitErrorFromResult(C.zlink_actor_join_spot_go(a.handle, spot.raw(), part, C.zlink_send_flags_t(flags), C.uint32_t(requestTimeoutMillis(timeout)), C.uintptr_t(cb)))
	}, callback)
}

func (a *Actor) Leave(spot *Spot) error {
	if a == nil || a.closed || a.handle == nil || spot == nil || spot.core == nil || spot.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_actor_leave_spot(a.handle, spot.raw()))
}

func (a *Actor) RecvPart(flags RecvFlags) (*ActorPart, error) {
	if a == nil || a.closed || a.handle == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return recvActorPartFromHandle(a.handle, flags)
}

func (a *Actor) SendBoundSession(flags SendFlags, message *Message) error {
	if a == nil || a.closed || a.handle == nil || message == nil || message.closed {
		return &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	cloned, err := message.clone()
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart([]*Message{cloned})
	if err != nil {
		_ = cloned.Close()
		return err
	}
	err = submitErrorFromResult(C.zlink_actor_send_bound_session_msg(a.handle, prepared.ptr(), C.zlink_send_flags_t(flags)))
	if err != nil {
		_ = prepared.restore()
		return err
	}
	prepared.commit()
	message.moved()
	return nil
}

func (a *Actor) SendBoundSessionPacket(flags SendFlags, header *Message, body *Message) error {
	if a == nil || a.closed || a.handle == nil || header == nil || body == nil || header.closed || body.closed {
		return &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	cloned, err := cloneParts([]*Message{header, body})
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return err
	}
	err = submitErrorFromResult(C.zlink_actor_send_bound_session_packet(a.handle, &prepared.native[0], &prepared.native[1], C.zlink_send_flags_t(flags)))
	if err != nil {
		_ = prepared.restore()
		return err
	}
	prepared.commit()
	header.moved()
	body.moved()
	return nil
}

func (s *Spot) RecvActorJoin(flags RecvFlags) (*ActorJoinInfo, *Message, error) {
	if s == nil || s.core == nil || s.core.closed {
		return nil, nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var rawInfo C.zlink_actor_join_info_t
	var rawMsg C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&rawMsg)); err != nil {
		return nil, nil, err
	}
	if err := recvErrorFromResult(C.zlink_spot_actor_join_recv(s.raw(), &rawInfo, &rawMsg, C.zlink_recv_flags_t(flags))); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&rawMsg))
		return nil, nil, err
	}
	msg := &Message{}
	if err := configErrorFromResult(C.zlink_msg_init(&msg.msg)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&rawMsg))
		return nil, nil, err
	}
	if err := configErrorFromResult(C.zlink_msg_move(&msg.msg, &rawMsg)); err != nil {
		_ = msg.Close()
		_ = configErrorFromResult(C.zlink_msg_close(&rawMsg))
		return nil, nil, err
	}
	return actorJoinInfoFromC(rawInfo), msg, nil
}

func (s *Spot) ReplyActorJoin(info *ActorJoinInfo, accepted bool, message *Message) error {
	if s == nil || s.core == nil || s.core.closed || info == nil || message == nil || message.closed {
		return &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	cloned, err := message.clone()
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart([]*Message{cloned})
	if err != nil {
		_ = cloned.Close()
		return err
	}
	var accept C.uint32_t
	if accepted {
		accept = 1
	}
	err = submitErrorFromResult(C.zlink_spot_actor_join_reply(s.raw(), &info.raw, accept, prepared.ptr()))
	if err != nil {
		_ = prepared.restore()
		return err
	}
	prepared.commit()
	message.moved()
	return nil
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

func (d *Discovery) ResolveActor(actorID string) (*ActorRoute, error) {
	if d == nil || d.closed {
		return nil, stateError("discovery is closed")
	}
	return withActorIDCString(actorID, func(actorIDC *C.char) (*ActorRoute, error) {
		var raw C.zlink_actor_route_t
		if err := configErrorFromResult(C.zlink_discovery_resolve_actor(d.raw(), actorIDC, &raw)); err != nil {
			return nil, err
		}
		return actorRouteFromC(raw), nil
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
	return requestErrorFromResult(C.zlink_stream_bind_actor(nodeHandle, s.raw(), &session, &rawActor, C.uint32_t(requestTimeoutMillis(timeout))))
}

func (s *StreamSocket) UnbindActor(node *SpotNode, sessionRID RoutingID, actorID string, timeout time.Duration) error {
	if node == nil || s == nil || s.core == nil || s.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	nodeHandle, err := node.handleOrError()
	if err != nil {
		return err
	}
	session := sessionRID.toC()
	return withActorIDCStringErr(actorID, func(actorIDC *C.char) error {
		return requestErrorFromResult(C.zlink_stream_unbind_actor(nodeHandle, s.raw(), &session, actorIDC, C.uint32_t(requestTimeoutMillis(timeout))))
	})
}

func (s *StreamSocket) SendBoundActor(node *SpotNode, sessionRID RoutingID, actorID string, flags SendFlags, parts ...*Message) error {
	if node == nil || s == nil || s.core == nil || s.core.closed {
		return &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	nodeHandle, err := node.handleOrError()
	if err != nil {
		return err
	}
	session := sessionRID.toC()
	return withActorIDCStringErr(actorID, func(actorIDC *C.char) error {
		return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_stream_send_bound_actor_part(nodeHandle, s.raw(), &session, actorIDC, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
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

func recvActorPartFromHandle(actor unsafe.Pointer, flags RecvFlags) (*ActorPart, error) {
	if actor == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var info C.zlink_actor_recv_info_t
	var part C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
		return nil, err
	}
	var more C.zlink_part_flag_t
	if err := recvErrorFromResult(C.zlink_actor_recv_part(actor, &info, &part, &more, C.zlink_recv_flags_t(flags))); err != nil {
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
		Actor:         actorRefFromC(raw.actor),
		SourceNodeRID: routingIDFromC(raw.source_node_rid),
		Flags:         uint32(raw.flags),
		raw:           raw,
	}
}

func actorRouteFromC(raw C.zlink_actor_route_t) *ActorRoute {
	return &ActorRoute{
		Actor:         actorRefFromC(raw.actor),
		Joined:        raw.joined != 0,
		JoinedSpotRID: routingIDFromC(raw.joined_spot_rid),
	}
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
