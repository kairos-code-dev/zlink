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
	// RouteKind identifies an owner-scoped discovery route table.
	RouteKind = impl.RouteKind
	// SpotRole is the pub/sub role of a spot subject.
	SpotRole = impl.SpotRole
	// Discovery learns peer routes from a registry and resolves spots and actors for a fixed channel.
	Discovery = impl.Discovery
	// SpotRoute is the resolved route to a spot: the spot, its owning node, and its kind.
	SpotRoute = impl.SpotRoute
	// DiscoveryRoute is a resolved custom route entry.
	DiscoveryRoute = impl.DiscoveryRoute
	// MemberPeerEntry is one member peer registered on a channel.
	MemberPeerEntry = impl.MemberPeerEntry
)

const (
	// AutoConnectInvalid is the unset auto-connect type.
	AutoConnectInvalid = impl.AutoConnectInvalid
	// AutoConnectRouteMesh wires a full mesh of ROUTER connections between peers.
	AutoConnectRouteMesh = impl.AutoConnectRouteMesh
	// AutoConnectClientServer wires a client-server star: clients connect to servers.
	AutoConnectClientServer = impl.AutoConnectClientServer
	// AutoConnectDealerMesh wires a full mesh of DEALER connections between peers.
	AutoConnectDealerMesh = impl.AutoConnectDealerMesh
	// AutoConnectFanout wires a publish/subscribe fan-out from publishers to subscribers.
	AutoConnectFanout = impl.AutoConnectFanout
	// AutoConnectSpotMesh wires a full mesh of spot connections between peers.
	AutoConnectSpotMesh = impl.AutoConnectSpotMesh
	// ServiceRoleInvalid is the unset service role.
	ServiceRoleInvalid = impl.ServiceRoleInvalid
	// ServiceRoleSpot is the spot messaging role.
	ServiceRoleSpot = impl.ServiceRoleSpot
	// ServiceRoleRouter is the ROUTER endpoint role.
	ServiceRoleRouter = impl.ServiceRoleRouter
	// ServiceRoleDealer is the DEALER endpoint role.
	ServiceRoleDealer = impl.ServiceRoleDealer
	// ServiceRolePub is the publisher role.
	ServiceRolePub = impl.ServiceRolePub
	// ServiceRoleSub is the subscriber role.
	ServiceRoleSub = impl.ServiceRoleSub
	// ServiceKindDiscovery identifies a discovery service entry.
	ServiceKindDiscovery = impl.ServiceKindDiscovery
	// ServiceKindSpotSub identifies the subscribe side of a spot entry.
	ServiceKindSpotSub = impl.ServiceKindSpotSub
	// ServiceKindSpotPub identifies the publish side of a spot entry.
	ServiceKindSpotPub = impl.ServiceKindSpotPub
	// ServiceKindSocket identifies a plain socket entry.
	ServiceKindSocket = impl.ServiceKindSocket
	// SpotKindInvalid is the unset spot kind.
	SpotKindInvalid = impl.SpotKindInvalid
	// SpotKindEntry is a node's well-known entry spot.
	SpotKindEntry = impl.SpotKindEntry
	// SpotKindUser is a user-created spot.
	SpotKindUser = impl.SpotKindUser
	// SubjectKindNone indicates no subject.
	SubjectKindNone = impl.SubjectKindNone
	// SubjectKindTopic matches by exact topic string.
	SubjectKindTopic = impl.SubjectKindTopic
	// SubjectKindPattern matches by pattern.
	SubjectKindPattern = impl.SubjectKindPattern
	// RouteKindInvalid is the unset discovery route kind.
	RouteKindInvalid = impl.RouteKindInvalid
	// RouteKindActor identifies a route keyed by actor id.
	RouteKindActor = impl.RouteKindActor
	// RouteKindSpotName identifies a route keyed by spot name.
	RouteKindSpotName = impl.RouteKindSpotName
	// RouteKindActorSession identifies a route keyed by actor session.
	RouteKindActorSession = impl.RouteKindActorSession
	// SpotRolePub is the publisher role of a spot subject.
	SpotRolePub = impl.SpotRolePub
	// SpotRoleSub is the subscriber role of a spot subject.
	SpotRoleSub = impl.SpotRoleSub
)
