// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/service"

type (
	// AutoConnectType is how a discovery service automatically wires connections between peers.
	AutoConnectType = impl.AutoConnectType
	// ServiceRole is the messaging role a service plays in the topology.
	ServiceRole = impl.ServiceRole
	// ServiceKind is the kind of service a topology entry represents.
	ServiceKind = impl.ServiceKind
	// SpotKind is the kind of a spot (entry or user).
	SpotKind = impl.SpotKind
	// SubjectKind is how a subscription subject is matched.
	SubjectKind = impl.SubjectKind
	// SpotRole is the pub/sub role of a spot subject.
	SpotRole = impl.SpotRole
	// Discovery learns peer routes from a registry and resolves spots and actors for a fixed channel.
	Discovery = impl.Discovery
	// SpotRoute is the resolved route to a spot: the spot, its owning node, and its kind.
	SpotRoute = impl.SpotRoute
	// MemberPeerEntry is one member peer registered on a channel.
	MemberPeerEntry = impl.MemberPeerEntry
)

const (
	AutoConnectInvalid      = impl.AutoConnectInvalid
	AutoConnectRouteMesh    = impl.AutoConnectRouteMesh
	AutoConnectClientServer = impl.AutoConnectClientServer
	AutoConnectDealerMesh   = impl.AutoConnectDealerMesh
	AutoConnectFanout       = impl.AutoConnectFanout
	AutoConnectSpotMesh     = impl.AutoConnectSpotMesh
	ServiceRoleInvalid      = impl.ServiceRoleInvalid
	ServiceRoleSpot         = impl.ServiceRoleSpot
	ServiceRoleRouter       = impl.ServiceRoleRouter
	ServiceRoleDealer       = impl.ServiceRoleDealer
	ServiceRolePub          = impl.ServiceRolePub
	ServiceRoleSub          = impl.ServiceRoleSub
	ServiceKindDiscovery    = impl.ServiceKindDiscovery
	ServiceKindSpotSub      = impl.ServiceKindSpotSub
	ServiceKindSpotPub      = impl.ServiceKindSpotPub
	ServiceKindSocket       = impl.ServiceKindSocket
	SpotKindInvalid         = impl.SpotKindInvalid
	SpotKindEntry           = impl.SpotKindEntry
	SpotKindUser            = impl.SpotKindUser
	SubjectKindNone         = impl.SubjectKindNone
	SubjectKindTopic        = impl.SubjectKindTopic
	SubjectKindPattern      = impl.SubjectKindPattern
	SpotRolePub             = impl.SpotRolePub
	SpotRoleSub             = impl.SpotRoleSub
)
