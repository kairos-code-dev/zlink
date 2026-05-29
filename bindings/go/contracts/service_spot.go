// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/service"

type (
	SpotNode                  = impl.SpotNode
	SpotNodeMode              = impl.SpotNodeMode
	SpotNodeSocketOwner       = impl.SpotNodeSocketOwner
	SpotNodeOption            = impl.SpotNodeOption
	SpotNodeOptions           = impl.SpotNodeOptions
	SpotNodeSocketFilter      = impl.SpotNodeSocketFilter
	SpotNodeSocketEntry       = impl.SpotNodeSocketEntry
	Spot                      = impl.Spot
	ActorRef                  = impl.ActorRef
	ActorRoute                = impl.ActorRoute
	ActorJoinRequest          = impl.ActorJoinRequest
	ActorRecvInfo             = impl.ActorRecvInfo
	ActorJoinInfo             = impl.ActorJoinInfo
	ActorPart                 = impl.ActorPart
	SpotNodeSpotEntry         = impl.SpotNodeSpotEntry
	SpotNodeActorEntry        = impl.SpotNodeActorEntry
	Actor                     = impl.Actor
	ActorJoinResult           = impl.ActorJoinResult
	ActorJoinCompletion       = impl.ActorJoinCompletion
	ActorLookupResult         = impl.ActorLookupResult
	ActorLookupCompletion     = impl.ActorLookupCompletion
	SpotActorLifecycleInfo    = impl.SpotActorLifecycleInfo
	ActorJoinOp               = impl.ActorJoinOp
	ActorJoinSubmitOp         = impl.ActorJoinSubmitOp
	ActorJoinCallbackSubmitOp = impl.ActorJoinCallbackSubmitOp
	ActorJoinReplyOp          = impl.ActorJoinReplyOp
	ActorLeaveOp              = impl.ActorLeaveOp
	ActorDestroyOp            = impl.ActorDestroyOp
	ActorLookupOp             = impl.ActorLookupOp
	ActorBindOp               = impl.ActorBindOp
	ActorUnbindOp             = impl.ActorUnbindOp
	SpotNodeState             = impl.SpotNodeState
	SpotPeerSource            = impl.SpotPeerSource
	SpotPeerKind              = impl.SpotPeerKind
	SpotPeerState             = impl.SpotPeerState
	SpotNodeStatus            = impl.SpotNodeStatus
	SpotNodePeerEntry         = impl.SpotNodePeerEntry
	SpotNodePeerFilter        = impl.SpotNodePeerFilter
	SpotNodeSubjectEntry      = impl.SpotNodeSubjectEntry
	SpotNodeSubjectFilter     = impl.SpotNodeSubjectFilter
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
