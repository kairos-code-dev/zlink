// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/service"

type (
	// SpotNode hosts spots and actors, tunes their sockets, and exposes the node's peers, subjects, and topology.
	SpotNode = impl.SpotNode
	// SpotNodeMode selects which messaging patterns a spot node enables.
	SpotNodeMode = impl.SpotNodeMode
	// SpotNodeSocketOwner identifies which component owns a spot node socket.
	SpotNodeSocketOwner = impl.SpotNodeSocketOwner
	// SpotNodeOption is a configurable option of a spot node.
	SpotNodeOption = impl.SpotNodeOption
	// SpotNodeOptions are options for creating a spot node.
	SpotNodeOptions = impl.SpotNodeOptions
	// SpotNodeSocketFilter filters a spot node socket query; zero fields match anything.
	SpotNodeSocketFilter = impl.SpotNodeSocketFilter
	// SpotNodeSocketEntry is one socket owned within a spot node and its monitored status.
	SpotNodeSocketEntry = impl.SpotNodeSocketEntry
	// Spot is a multi-role messaging endpoint that can publish, subscribe, route, request, reply, and host actors.
	Spot = impl.Spot
	// ActorRef references an actor: the node hosting it, its id, and its generation.
	ActorRef = impl.ActorRef
	// ActorRoute is the resolved route to an actor: which spot it currently lives on.
	ActorRoute = impl.ActorRoute
	// ActorJoinRequest is a pending request from an actor to join a spot, awaiting a reply.
	ActorJoinRequest = impl.ActorJoinRequest
	// ActorRecvInfo is metadata about a message received for an actor.
	ActorRecvInfo = impl.ActorRecvInfo
	// ActorJoinInfo holds details of an actor-join request: the actors and spots on each side.
	ActorJoinInfo = impl.ActorJoinInfo
	// ActorPart is one part of a message received for an actor.
	ActorPart = impl.ActorPart
	// SpotNodeSpotEntry is one spot hosted on a spot node and its actor counts.
	SpotNodeSpotEntry = impl.SpotNodeSpotEntry
	// SpotNodeActorEntry is one actor hosted on a spot node and its current placement.
	SpotNodeActorEntry = impl.SpotNodeActorEntry
	// Actor is a handle to a local actor: join/leave spots, receive its messages, and send to its bound session.
	Actor = impl.Actor
	// ActorJoinResult is the outcome of an actor join.
	ActorJoinResult = impl.ActorJoinResult
	// ActorJoinCompletion carries the result and reply parts of a completed actor join.
	ActorJoinCompletion = impl.ActorJoinCompletion
	// ActorLookupResult is the outcome of an actor lookup.
	ActorLookupResult = impl.ActorLookupResult
	// ActorLookupCompletion carries the result of a completed actor lookup.
	ActorLookupCompletion = impl.ActorLookupCompletion
	// SpotActorLifecycleInfo holds details of an actor lifecycle change, before and after.
	SpotActorLifecycleInfo = impl.SpotActorLifecycleInfo
	// ActorJoinOp builds an actor join: add parts, then submit and await the result.
	ActorJoinOp = impl.ActorJoinOp
	// ActorJoinSubmitOp accepts further parts, timeout, flags, and the terminal submit of an actor join.
	ActorJoinSubmitOp = impl.ActorJoinSubmitOp
	// ActorJoinCallbackSubmitOp is the callback-submission stage of an actor join.
	ActorJoinCallbackSubmitOp = impl.ActorJoinCallbackSubmitOp
	// ActorJoinReplyOp builds a reply to an actor-join request; a zero-part reply is allowed.
	ActorJoinReplyOp = impl.ActorJoinReplyOp
	// ActorLeaveOp builds an actor leave.
	ActorLeaveOp = impl.ActorLeaveOp
	// ActorDestroyOp builds an actor destroy.
	ActorDestroyOp = impl.ActorDestroyOp
	// ActorLookupOp builds a remote actor lookup.
	ActorLookupOp = impl.ActorLookupOp
	// ActorBindOp builds an actor-to-session bind.
	ActorBindOp = impl.ActorBindOp
	// ActorUnbindOp builds an actor-from-session unbind.
	ActorUnbindOp = impl.ActorUnbindOp
	// SpotNodeState is the overall readiness state of a spot node.
	SpotNodeState = impl.SpotNodeState
	// SpotPeerSource is how a spot peer became known to the node.
	SpotPeerSource = impl.SpotPeerSource
	// SpotPeerKind is the connection style of a spot peer.
	SpotPeerKind = impl.SpotPeerKind
	// SpotPeerState is the connection state of a spot peer.
	SpotPeerState = impl.SpotPeerState
	// SpotNodeStatus is a snapshot of a spot node's status and peer/subject counts.
	SpotNodeStatus = impl.SpotNodeStatus
	// SpotNodePeerEntry is one peer of a spot node and its connection details.
	SpotNodePeerEntry = impl.SpotNodePeerEntry
	// SpotNodePeerFilter filters a spot node peer query; zero fields match anything.
	SpotNodePeerFilter = impl.SpotNodePeerFilter
	// SpotNodeSubjectEntry is one subject (topic or pattern) served by a spot node.
	SpotNodeSubjectEntry = impl.SpotNodeSubjectEntry
	// SpotNodeSubjectFilter filters a spot node subject query; zero fields match anything.
	SpotNodeSubjectFilter = impl.SpotNodeSubjectFilter
)

const (
	SpotNodeModePubSub                = impl.SpotNodeModePubSub
	SpotNodeModeRouted                = impl.SpotNodeModeRouted
	SpotNodeModeAll                   = impl.SpotNodeModeAll
	SpotNodeSocketOwnerAny            = impl.SpotNodeSocketOwnerAny
	SpotNodeSocketOwnerNode           = impl.SpotNodeSocketOwnerNode
	SpotNodeSocketOwnerSpot           = impl.SpotNodeSocketOwnerSpot
	SpotNodeOptionRouterHwmProfile    = impl.SpotNodeOptionRouterHwmProfile
	SpotNodeOptionRouterHighWaterMark = impl.SpotNodeOptionRouterHighWaterMark
	SpotNodeOptionPubSubHwmProfile    = impl.SpotNodeOptionPubSubHwmProfile
	SpotNodeOptionPubSubHighWaterMark = impl.SpotNodeOptionPubSubHighWaterMark
	SpotNodeOptionDispatchWorkersMin  = impl.SpotNodeOptionDispatchWorkersMin
	SpotNodeOptionDispatchWorkersMax  = impl.SpotNodeOptionDispatchWorkersMax
	SpotNodeStateIdle                 = impl.SpotNodeStateIdle
	SpotNodeStateConnecting           = impl.SpotNodeStateConnecting
	SpotNodeStatePartialReady         = impl.SpotNodeStatePartialReady
	SpotNodeStateReady                = impl.SpotNodeStateReady
	SpotNodeStateError                = impl.SpotNodeStateError
	SpotPeerSourceManual              = impl.SpotPeerSourceManual
	SpotPeerSourceDiscovery           = impl.SpotPeerSourceDiscovery
	SpotPeerSourceMixed               = impl.SpotPeerSourceMixed
	SpotPeerKindSpotMesh              = impl.SpotPeerKindSpotMesh
	SpotPeerKindRouterChannel         = impl.SpotPeerKindRouterChannel
	SpotPeerStateConfigured           = impl.SpotPeerStateConfigured
	SpotPeerStateConnecting           = impl.SpotPeerStateConnecting
	SpotPeerStateConnected            = impl.SpotPeerStateConnected
)

var RemoteActorRef = impl.RemoteActorRef
