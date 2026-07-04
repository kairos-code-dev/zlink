// SPDX-License-Identifier: MPL-2.0

package native

/*
#include "zlink.h"
*/
import "C"

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
	Actor           ActorRef
	CurrentSpotRID  RoutingID
	CurrentSpotKind SpotKind
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
	RequestID        uint64
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

// SpotNodeSpotEntry is an entry from SpotNode.Spots.
type SpotNodeSpotEntry struct {
	SpotRID                 RoutingID
	SpotKind                SpotKind
	DispatchHandlerAttached bool
	JoinedActorCount        uint32
	PendingActorJoinCount   uint32
	RouteSynced             bool
	LastChangedMs           uint64
}

// SpotNodeActorEntry is an entry from SpotNode.Actors.
type SpotNodeActorEntry struct {
	Actor               ActorRef
	CurrentSpotRID      RoutingID
	CurrentSpotKind     SpotKind
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
