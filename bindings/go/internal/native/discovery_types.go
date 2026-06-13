// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import "unsafe"

type AutoConnectType uint32

const (
	AutoConnectInvalid      AutoConnectType = AutoConnectType(C.ZLINK_AUTO_CONNECT_INVALID)
	AutoConnectRouteMesh    AutoConnectType = AutoConnectType(C.ZLINK_AUTO_CONNECT_ROUTE_MESH)
	AutoConnectClientServer AutoConnectType = AutoConnectType(C.ZLINK_AUTO_CONNECT_CLIENT_SERVER)
	AutoConnectDealerMesh   AutoConnectType = AutoConnectType(C.ZLINK_AUTO_CONNECT_DEALER_MESH)
	AutoConnectFanout       AutoConnectType = AutoConnectType(C.ZLINK_AUTO_CONNECT_FANOUT)
	AutoConnectSpotMesh     AutoConnectType = AutoConnectType(C.ZLINK_AUTO_CONNECT_SPOT_MESH)
)

type ServiceRole uint16

const (
	ServiceRoleInvalid ServiceRole = ServiceRole(C.ZLINK_SERVICE_ROLE_INVALID)
	ServiceRoleSpot    ServiceRole = ServiceRole(C.ZLINK_SERVICE_ROLE_SPOT)
	ServiceRoleRouter  ServiceRole = ServiceRole(C.ZLINK_SERVICE_ROLE_ROUTER)
	ServiceRoleDealer  ServiceRole = ServiceRole(C.ZLINK_SERVICE_ROLE_DEALER)
	ServiceRolePub     ServiceRole = ServiceRole(C.ZLINK_SERVICE_ROLE_PUB)
	ServiceRoleSub     ServiceRole = ServiceRole(C.ZLINK_SERVICE_ROLE_SUB)
)

type ServiceKind uint32

const (
	ServiceKindDiscovery ServiceKind = ServiceKind(C.ZLINK_SERVICE_KIND_DISCOVERY)
	ServiceKindSpotSub   ServiceKind = ServiceKind(C.ZLINK_SERVICE_KIND_SPOT_SUB)
	ServiceKindSpotPub   ServiceKind = ServiceKind(C.ZLINK_SERVICE_KIND_SPOT_PUB)
	ServiceKindSocket    ServiceKind = ServiceKind(C.ZLINK_SERVICE_KIND_SOCKET)
)

type SpotKind uint32

const (
	SpotKindInvalid SpotKind = SpotKind(C.ZLINK_SPOT_KIND_INVALID)
	SpotKindEntry   SpotKind = SpotKind(C.ZLINK_SPOT_KIND_ENTRY)
	SpotKindUser    SpotKind = SpotKind(C.ZLINK_SPOT_KIND_USER)
)

type SubjectKind uint32

const (
	SubjectKindNone    SubjectKind = SubjectKind(C.ZLINK_SERVICE_EVENT_SUBJECT_NONE)
	SubjectKindTopic   SubjectKind = SubjectKind(C.ZLINK_SERVICE_EVENT_SUBJECT_TOPIC)
	SubjectKindPattern SubjectKind = SubjectKind(C.ZLINK_SERVICE_EVENT_SUBJECT_PATTERN)
)

type RouteKind uint32

const (
	RouteKindInvalid      RouteKind = RouteKind(C.ZLINK_ROUTE_KIND_INVALID)
	RouteKindActor        RouteKind = RouteKind(C.ZLINK_ROUTE_KIND_ACTOR)
	RouteKindSpotName     RouteKind = RouteKind(C.ZLINK_ROUTE_KIND_SPOT_NAME)
	RouteKindActorSession RouteKind = RouteKind(C.ZLINK_ROUTE_KIND_ACTOR_SESSION)
)

type DiscoveryRoute struct {
	OwnerRoutingID RoutingID
	Value          *Message
}

type SpotRole uint32

const (
	SpotRolePub SpotRole = SpotRole(C.ZLINK_SPOT_ROLE_PUB)
	SpotRoleSub SpotRole = SpotRole(C.ZLINK_SPOT_ROLE_SUB)
)

type SpotNodeState uint32

const (
	SpotNodeStateIdle         SpotNodeState = SpotNodeState(C.ZLINK_SPOT_NODE_STATE_IDLE)
	SpotNodeStateConnecting   SpotNodeState = SpotNodeState(C.ZLINK_SPOT_NODE_STATE_CONNECTING)
	SpotNodeStatePartialReady SpotNodeState = SpotNodeState(C.ZLINK_SPOT_NODE_STATE_PARTIAL_READY)
	SpotNodeStateReady        SpotNodeState = SpotNodeState(C.ZLINK_SPOT_NODE_STATE_READY)
	SpotNodeStateError        SpotNodeState = SpotNodeState(C.ZLINK_SPOT_NODE_STATE_ERROR)
)

type SpotPeerSource uint32

const (
	SpotPeerSourceManual    SpotPeerSource = SpotPeerSource(C.ZLINK_SPOT_PEER_SOURCE_MANUAL)
	SpotPeerSourceDiscovery SpotPeerSource = SpotPeerSource(C.ZLINK_SPOT_PEER_SOURCE_DISCOVERY)
	SpotPeerSourceMixed     SpotPeerSource = SpotPeerSource(C.ZLINK_SPOT_PEER_SOURCE_MIXED)
)

type SpotPeerKind uint32

const (
	SpotPeerKindSpotMesh      SpotPeerKind = SpotPeerKind(C.ZLINK_SPOT_PEER_KIND_SPOT_MESH)
	SpotPeerKindRouterChannel SpotPeerKind = SpotPeerKind(C.ZLINK_SPOT_PEER_KIND_ROUTER_CHANNEL)
)

type SpotPeerState uint32

const (
	SpotPeerStateConfigured SpotPeerState = SpotPeerState(C.ZLINK_SPOT_PEER_STATE_CONFIGURED)
	SpotPeerStateConnecting SpotPeerState = SpotPeerState(C.ZLINK_SPOT_PEER_STATE_CONNECTING)
	SpotPeerStateConnected  SpotPeerState = SpotPeerState(C.ZLINK_SPOT_PEER_STATE_CONNECTED)
)

type RegistryState uint32

const (
	RegistryStateIdle     RegistryState = RegistryState(C.ZLINK_REGISTRY_STATE_IDLE)
	RegistryStateActive   RegistryState = RegistryState(C.ZLINK_REGISTRY_STATE_ACTIVE)
	RegistryStateDegraded RegistryState = RegistryState(C.ZLINK_REGISTRY_STATE_DEGRADED)
	RegistryStateError    RegistryState = RegistryState(C.ZLINK_REGISTRY_STATE_ERROR)
)

type TopologySource uint32

const (
	TopologySourceManual    TopologySource = TopologySource(C.ZLINK_TOPOLOGY_SOURCE_MANUAL)
	TopologySourceDiscovery TopologySource = TopologySource(C.ZLINK_TOPOLOGY_SOURCE_DISCOVERY)
	TopologySourceRegistry  TopologySource = TopologySource(C.ZLINK_TOPOLOGY_SOURCE_REGISTRY)
)

type TopologyState uint32

const (
	TopologyStateDiscovered TopologyState = TopologyState(C.ZLINK_TOPOLOGY_STATE_DISCOVERED)
	TopologyStateConnecting TopologyState = TopologyState(C.ZLINK_TOPOLOGY_STATE_CONNECTING)
	TopologyStateReady      TopologyState = TopologyState(C.ZLINK_TOPOLOGY_STATE_READY)
	TopologyStateLost       TopologyState = TopologyState(C.ZLINK_TOPOLOGY_STATE_LOST)
	TopologyStateError      TopologyState = TopologyState(C.ZLINK_TOPOLOGY_STATE_ERROR)
	TopologyStateStopped    TopologyState = TopologyState(C.ZLINK_TOPOLOGY_STATE_STOPPED)
)

type Discovery struct {
	handle unsafe.Pointer
	closed bool
}

type Registry struct {
	handle unsafe.Pointer
	closed bool
}

type RegistryQueryClient struct {
	handle unsafe.Pointer
	closed bool
}

type SpotNodeStatus struct {
	ChannelName                   string
	LocalEndpoint                 string
	NodeRoutingID                 RoutingID
	State                         SpotNodeState
	ConfiguredPeerCount           uint32
	ActivePeerCount               uint32
	ConnectedPeerCount            uint32
	SubjectCount                  uint32
	ReadySubjectCount             uint32
	DisconnectedSubTargetCount    uint32
	DisconnectedRoutedTargetCount uint32
	LastError                     int32
	LastChangedMs                 uint64
}

type SpotNodePeerEntry struct {
	ChannelName      string
	LocalEndpoint    string
	PeerEndpoint     string
	Source           SpotPeerSource
	Kind             SpotPeerKind
	State            SpotPeerState
	Weight           uint32
	ConnectedSinceMs uint64
	LastChangedMs    uint64
}

type SpotNodePeerFilter struct {
	PeerEndpoint *string
	Source       *SpotPeerSource
	State        *SpotPeerState
}

type SpotNodeSubjectEntry struct {
	Role            SpotRole
	Subject         string
	SubjectKind     SubjectKind
	ReadyPeerCount  uint32
	ActivePeerCount uint32
	LastChangedMs   uint64
}

type SpotNodeSubjectFilter struct {
	Role        *SpotRole
	Subject     *string
	SubjectKind *SubjectKind
}

type RegistryStatus struct {
	RegistryID                 uint32
	BindEndpoint               string
	State                      RegistryState
	TopologyEntryCount         uint32
	PeerRegistryCount          uint32
	ConnectedPeerRegistryCount uint32
	ListSeq                    uint64
	LastError                  int32
	LastChangedMs              uint64
}

type RegistryServiceSummaryEntry struct {
	AutoConnectType AutoConnectType
	ServiceRole     ServiceRole
	ChannelName     string
	TotalCount      uint32
	ConnectingCount uint32
	ReadyCount      uint32
	ErrorCount      uint32
	StoppedCount    uint32
	LastReportedMs  uint64
}

type RegistryServiceSummaryFilter struct {
	AutoConnectType *AutoConnectType
	ServiceRole     *ServiceRole
	ChannelName     *string
}

type MemberPeerEntry struct {
	AutoConnectType AutoConnectType
	ServiceRole     ServiceRole
	ChannelName     string
	Endpoint        string
	RoutingID       RoutingID
	Weight          uint32
	Value           int64
}

type RegistryTopologyEntry struct {
	AutoConnectType AutoConnectType
	RoutingID       RoutingID
	ServiceKind     ServiceKind
	ServiceRole     ServiceRole
	ChannelName     string
	Endpoint        string
	Source          TopologySource
	State           TopologyState
	DesiredCount    uint32
	ReadyCount      uint32
	ErrorCode       uint32
	LastReportedMs  uint64
	SpotKind        SpotKind
}

type SpotRoute struct {
	SpotRID      RoutingID
	OwnerNodeRID RoutingID
	SpotKind     SpotKind
}

type RegistryTopologyFilter struct {
	AutoConnectType *AutoConnectType
	ServiceKind     *ServiceKind
	ServiceRole     *ServiceRole
	ChannelName     *string
	RoutingID       *RoutingID
	State           *TopologyState
	Source          *TopologySource
}

func (e *MemberPeerEntry) HasRoutingID() bool {
	return e != nil && serviceEntryHasRoutingID(e.RoutingID)
}

func (e *RegistryTopologyEntry) HasRoutingID() bool {
	return e != nil && serviceEntryHasRoutingID(e.RoutingID)
}

func (s *SpotNodeStatus) HasNodeRoutingID() bool {
	return s != nil && spotNodeHasRoutingID(s.NodeRoutingID)
}
