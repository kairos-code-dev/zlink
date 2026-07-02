// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

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
	RouteKindInvalid      RouteKind = 0
	RouteKindActor        RouteKind = 1
	RouteKindSpotName     RouteKind = 2
	RouteKindActorSession RouteKind = 3
)

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

func (s *SpotNodeStatus) HasNodeRoutingID() bool {
	return s != nil && spotNodeHasRoutingID(s.NodeRoutingID)
}
