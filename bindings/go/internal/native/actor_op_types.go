// SPDX-License-Identifier: MPL-2.0

package native

import (
	"context"
	"time"
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
