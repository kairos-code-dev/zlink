// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

static inline int zlink_discovery_resolve_spot_go(void *discovery, const zlink_routing_id_t *spot_rid, zlink_routing_id_t *owner_node_rid_out) {
    return zlink_discovery_resolve_spot(discovery, spot_rid, owner_node_rid_out);
}
*/
import "C"

import (
	"errors"
	"strings"
	"syscall"
	"unsafe"
)

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

type SubjectKind uint32

const (
	SubjectKindNone    SubjectKind = SubjectKind(C.ZLINK_SERVICE_EVENT_SUBJECT_NONE)
	SubjectKindTopic   SubjectKind = SubjectKind(C.ZLINK_SERVICE_EVENT_SUBJECT_TOPIC)
	SubjectKindPattern SubjectKind = SubjectKind(C.ZLINK_SERVICE_EVENT_SUBJECT_PATTERN)
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
	ServiceName                   string
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
	ServiceName      string
	LocalEndpoint    string
	PeerEndpoint     string
	Source           SpotPeerSource
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

func newRegistry(ctx *Context) (*Registry, error) {
	if ctx == nil || ctx.closed {
		return nil, stateError("context is closed")
	}
	handle := C.zlink_registry_new(ctx.raw())
	if handle == nil {
		return nil, lastError()
	}
	return &Registry{handle: handle}, nil
}

func newDiscovery(ctx *Context, autoConnectType AutoConnectType, channelName string) (*Discovery, error) {
	if ctx == nil || ctx.closed {
		return nil, stateError("context is closed")
	}
	if err := validateChannelName(channelName); err != nil {
		return nil, err
	}
	channelNameC := C.CString(channelName)
	defer C.free(unsafe.Pointer(channelNameC))
	handle := C.zlink_discovery_new(ctx.raw(), C.zlink_auto_connect_type_t(autoConnectType), channelNameC)
	if handle == nil {
		return nil, lastError()
	}
	return &Discovery{handle: handle}, nil
}

func newRegistryQueryClient(ctx *Context) (*RegistryQueryClient, error) {
	if ctx == nil || ctx.closed {
		return nil, stateError("context is closed")
	}
	handle := C.zlink_registry_query_client_new(ctx.raw())
	if handle == nil {
		return nil, lastError()
	}
	return &RegistryQueryClient{handle: handle}, nil
}

func (d *Discovery) raw() unsafe.Pointer {
	if d == nil {
		return nil
	}
	return d.handle
}

func (r *Registry) raw() unsafe.Pointer {
	if r == nil {
		return nil
	}
	return r.handle
}

func (c *RegistryQueryClient) raw() unsafe.Pointer {
	if c == nil {
		return nil
	}
	return c.handle
}

func (d *Discovery) Close() error {
	if d == nil || d.closed {
		return nil
	}
	handle := d.handle
	if err := checkRC(C.zlink_discovery_destroy(&handle)); err != nil {
		return err
	}
	d.closed = true
	d.handle = nil
	return nil
}

func (r *Registry) Close() error {
	if r == nil || r.closed {
		return nil
	}
	handle := r.handle
	if err := checkRC(C.zlink_registry_destroy(&handle)); err != nil {
		return err
	}
	r.closed = true
	r.handle = nil
	return nil
}

func (c *RegistryQueryClient) Close() error {
	if c == nil || c.closed {
		return nil
	}
	handle := c.handle
	if err := checkRC(C.zlink_registry_query_destroy(&handle)); err != nil {
		return err
	}
	c.closed = true
	c.handle = nil
	return nil
}

func (d *Discovery) ConnectRegistry(endpoint string) error {
	return withDiscoveryCString(d, endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_discovery_connect_registry(d.raw(), cstr))
	})
}

func (d *Discovery) SetValue(value int64) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return checkRC(C.zlink_discovery_set_value(d.raw(), C.int64_t(value)))
}

func (d *Discovery) SetSpotOwnerSyncEnabled(enabled bool) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return setNativeBoolOption(d.raw(), d.closed, "discovery is closed", C.ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC, enabled)
}

func (d *Discovery) SpotOwnerSyncEnabled() (bool, error) {
	if d == nil || d.closed {
		return false, stateError("discovery is closed")
	}
	var value C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(ConfigResult(C.zlink_get_option(
		d.raw(),
		C.ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
		unsafe.Pointer(&value),
		&size,
	))); err != nil {
		return false, err
	}
	return value != 0, nil
}

func (d *Discovery) SetActorRouteSyncEnabled(enabled bool) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return setNativeBoolOption(d.raw(), d.closed, "discovery is closed", C.ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC, enabled)
}

func (d *Discovery) ActorRouteSyncEnabled() (bool, error) {
	if d == nil || d.closed {
		return false, stateError("discovery is closed")
	}
	var value C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(ConfigResult(C.zlink_get_option(
		d.raw(),
		C.ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
		unsafe.Pointer(&value),
		&size,
	))); err != nil {
		return false, err
	}
	return value != 0, nil
}

func (d *Discovery) ResolveSpot(spotRid RoutingID) (RoutingID, error) {
	if d == nil || d.closed {
		return RoutingID{}, stateError("discovery is closed")
	}
	nativeSpotRID := spotRid.toC()
	var ownerNodeRID C.zlink_routing_id_t
	if err := checkRC(C.zlink_discovery_resolve_spot_go(d.raw(), &nativeSpotRID, &ownerNodeRID)); err != nil {
		return RoutingID{}, err
	}
	return routingIDFromC(ownerNodeRID), nil
}

func (d *Discovery) GetValue() (int64, error) {
	if d == nil || d.closed {
		return 0, stateError("discovery is closed")
	}
	var value C.int64_t
	if err := checkRC(C.zlink_discovery_get_value(d.raw(), &value)); err != nil {
		return 0, err
	}
	return int64(value), nil
}

func (d *Discovery) MemberPeers() ([]MemberPeerEntry, error) {
	if d == nil || d.closed {
		return nil, stateError("discovery is closed")
	}
	return queryMemberPeers(func(entries *C.zlink_member_peer_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_discovery_member_peers(d.raw(), entries, count))
	})
}

func (d *Discovery) SetTLSClient(caCertPath string, hostname string, trustSystem bool) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return setTLSClient(d.raw(), caCertPath, hostname, trustSystem)
}

func (r *Registry) Bind(pubEndpoint string, routerEndpoint string) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return withCStringPair(pubEndpoint, routerEndpoint, func(pubC *C.char, routerC *C.char) error {
		return checkRC(C.zlink_registry_bind(r.raw(), pubC, routerC))
	})
}

func (r *Registry) SetTLSServer(certPath string, keyPath string, requireClientCert bool) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return setTLSServer(r.raw(), certPath, keyPath, requireClientCert)
}

func (r *Registry) SetTLSClient(caCertPath string, hostname string, trustSystem bool) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return setTLSClient(r.raw(), caCertPath, hostname, trustSystem)
}

func (r *Registry) SetId(registryID uint32) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return checkRC(C.zlink_registry_set_id(r.raw(), C.uint32_t(registryID)))
}

func (r *Registry) AddPeer(peerPubEndpoint string) error {
	return withRegistryCString(r, peerPubEndpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_registry_add_peer(r.raw(), cstr))
	})
}

func (r *Registry) SetHeartbeat(intervalMS uint32, timeoutMS uint32) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return checkRC(C.zlink_registry_set_heartbeat(r.raw(), C.uint32_t(intervalMS), C.uint32_t(timeoutMS)))
}

func (r *Registry) SetBroadcastInterval(intervalMS uint32) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return checkRC(C.zlink_registry_set_broadcast_interval(r.raw(), C.uint32_t(intervalMS)))
}

func (r *Registry) StatusSnapshot() (*RegistryStatus, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	var raw C.zlink_registry_status_t
	if err := checkRC(C.zlink_registry_status_snapshot(r.raw(), &raw)); err != nil {
		return nil, err
	}
	return registryStatusFromC(raw), nil
}

func (r *Registry) ServiceSummarySnapshot(filter *RegistryServiceSummaryFilter) ([]RegistryServiceSummaryEntry, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	var rawFilter *C.zlink_registry_service_summary_filter_t
	var cfilter C.zlink_registry_service_summary_filter_t
	if filter != nil {
		if err := validateRegistryServiceSummaryFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return queryRegistryServiceSummary(func(entries *C.zlink_registry_service_summary_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_registry_service_summary_snapshot(r.raw(), rawFilter, entries, count))
	})
}

func (r *Registry) MemberPeers(channelName string) ([]MemberPeerEntry, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	if err := validateChannelName(channelName); err != nil {
		return nil, err
	}
	var out []MemberPeerEntry
	channelNameC := C.CString(channelName)
	defer C.free(unsafe.Pointer(channelNameC))
	err := func() error {
		entries, err := queryMemberPeers(func(native *C.zlink_member_peer_entry_t, count *C.size_t) error {
			return checkRC(C.zlink_registry_member_peers(r.raw(), channelNameC, native, count))
		})
		if err != nil {
			return err
		}
		out = entries
		return nil
	}()
	return out, err
}

func (r *Registry) TopologySnapshot() ([]RegistryTopologyEntry, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	return queryRegistryTopology(func(entries *C.zlink_registry_topology_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_registry_topology_snapshot(r.raw(), entries, count))
	})
}

func (r *Registry) TopologyQuery(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	var rawFilter *C.zlink_registry_topology_filter_t
	var cfilter C.zlink_registry_topology_filter_t
	if filter != nil {
		if err := validateRegistryTopologyFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return queryRegistryTopology(func(entries *C.zlink_registry_topology_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_registry_topology_query(r.raw(), rawFilter, entries, count))
	})
}

func (c *RegistryQueryClient) Connect(endpoint string) error {
	return withRegistryQueryCString(c, endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_registry_query_client_connect(c.raw(), cstr))
	})
}

func (c *RegistryQueryClient) Snapshot(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error) {
	if c == nil || c.closed {
		return nil, stateError("registry query client is closed")
	}
	var rawFilter *C.zlink_registry_topology_filter_t
	var cfilter C.zlink_registry_topology_filter_t
	if filter != nil {
		if err := validateRegistryTopologyFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return queryRegistryTopology(func(entries *C.zlink_registry_topology_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_registry_query_snapshot(c.raw(), rawFilter, entries, count))
	})
}

func (n *SpotNode) StatusSnapshot() (*SpotNodeStatus, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	var raw C.zlink_spot_node_status_t
	if err := checkRC(C.zlink_spot_node_status_snapshot(n.raw(), &raw)); err != nil {
		return nil, err
	}
	return spotNodeStatusFromC(raw), nil
}

func (n *SpotNode) PeersSnapshot() ([]SpotNodePeerEntry, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	return querySpotNodePeers(func(entries *C.zlink_spot_node_peer_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_spot_node_peers_snapshot(n.raw(), entries, count))
	})
}

func (n *SpotNode) PeersQuery(filter *SpotNodePeerFilter) ([]SpotNodePeerEntry, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	var rawFilter *C.zlink_spot_node_peer_filter_t
	var cfilter C.zlink_spot_node_peer_filter_t
	if filter != nil {
		if err := validateSpotNodePeerFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return querySpotNodePeers(func(entries *C.zlink_spot_node_peer_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_spot_node_peers_query(n.raw(), rawFilter, entries, count))
	})
}

func (n *SpotNode) SubjectsSnapshot(filters ...*SpotNodeSubjectFilter) ([]SpotNodeSubjectEntry, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	var filter *SpotNodeSubjectFilter
	if len(filters) > 0 {
		filter = filters[0]
	}
	var rawFilter *C.zlink_spot_node_subject_filter_t
	var cfilter C.zlink_spot_node_subject_filter_t
	if filter != nil {
		if err := validateSpotNodeSubjectFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return querySpotNodeSubjects(func(entries *C.zlink_spot_node_subject_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_spot_node_subjects_snapshot(n.raw(), rawFilter, entries, count))
	})
}

func (n *SpotNode) InternalSocketsSnapshot(filter *SpotNodeSocketSnapshotFilter) ([]SpotNodeSocketSnapshotEntry, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	var rawFilter *C.zlink_spot_node_socket_snapshot_filter_t
	var cfilter C.zlink_spot_node_socket_snapshot_filter_t
	if filter != nil {
		if err := validateSpotNodeSocketSnapshotFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return querySpotNodeInternalSockets(func(entries *C.zlink_spot_node_socket_snapshot_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_spot_node_internal_sockets_snapshot(n.raw(), rawFilter, entries, count))
	})
}

func querySpotNodePeers(fetch func(*C.zlink_spot_node_peer_entry_t, *C.size_t) error) ([]SpotNodePeerEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_spot_node_peer_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		spotNodePeerEntryFromC,
	)
}

func querySpotNodeSubjects(fetch func(*C.zlink_spot_node_subject_entry_t, *C.size_t) error) ([]SpotNodeSubjectEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_spot_node_subject_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		spotNodeSubjectEntryFromC,
	)
}

func querySpotNodeInternalSockets(fetch func(*C.zlink_spot_node_socket_snapshot_entry_t, *C.size_t) error) ([]SpotNodeSocketSnapshotEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_spot_node_socket_snapshot_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		spotNodeSocketSnapshotEntryFromC,
	)
}

func queryRegistryServiceSummary(fetch func(*C.zlink_registry_service_summary_entry_t, *C.size_t) error) ([]RegistryServiceSummaryEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_registry_service_summary_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		registryServiceSummaryEntryFromC,
	)
}

func queryMemberPeers(fetch func(*C.zlink_member_peer_entry_t, *C.size_t) error) ([]MemberPeerEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_member_peer_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		memberPeerEntryFromC,
	)
}

func queryRegistryTopology(fetch func(*C.zlink_registry_topology_entry_t, *C.size_t) error) ([]RegistryTopologyEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_registry_topology_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		registryTopologyEntryFromC,
	)
}

func queryCountedSnapshot[CEntry any, T any](probe func() (int, error), fill func([]CEntry) (int, error), convert func(CEntry) T) ([]T, error) {
	const maxSnapshotRetries = 4

	for attempt := 0; attempt < maxSnapshotRetries; attempt++ {
		count, err := probe()
		if err != nil {
			return nil, err
		}
		if count == 0 {
			return nil, nil
		}

		native := make([]CEntry, count)
		count, err = fill(native)
		if err != nil {
			if isNativeErrorCode(err, int(C.ENOBUFS)) {
				continue
			}
			return nil, err
		}

		out := make([]T, count)
		for i := 0; i < count; i++ {
			out[i] = convert(native[i])
		}
		return out, nil
	}

	return nil, lastError()
}

func (f RegistryServiceSummaryFilter) toC() C.zlink_registry_service_summary_filter_t {
	var out C.zlink_registry_service_summary_filter_t
	if f.AutoConnectType != nil {
		out.auto_connect_type = C.zlink_auto_connect_type_t(*f.AutoConnectType)
	}
	if f.ServiceRole != nil {
		out.service_role = C.zlink_service_role_t(*f.ServiceRole)
	}
	if f.ChannelName != nil {
		mustCopyFixedCString(unsafe.Pointer(&out.channel_name[0]), 256, *f.ChannelName)
	}
	return out
}

func (f RegistryTopologyFilter) toC() C.zlink_registry_topology_filter_t {
	var out C.zlink_registry_topology_filter_t
	if f.AutoConnectType != nil {
		out.auto_connect_type = C.zlink_auto_connect_type_t(*f.AutoConnectType)
	}
	if f.ServiceKind != nil {
		out.service_kind = C.zlink_service_kind_t(*f.ServiceKind)
	}
	if f.ServiceRole != nil {
		out.service_role = C.zlink_service_role_t(*f.ServiceRole)
	}
	if f.ChannelName != nil {
		mustCopyFixedCString(unsafe.Pointer(&out.channel_name[0]), 256, *f.ChannelName)
	}
	if f.RoutingID != nil {
		out.routing_id = f.RoutingID.toC()
	}
	if f.State != nil {
		out.state = C.zlink_topology_state_t(*f.State)
	}
	if f.Source != nil {
		out.source = C.zlink_topology_source_t(*f.Source)
	}
	return out
}

func (f SpotNodePeerFilter) toC() C.zlink_spot_node_peer_filter_t {
	var out C.zlink_spot_node_peer_filter_t
	if f.PeerEndpoint != nil {
		mustCopyFixedCString(unsafe.Pointer(&out.peer_endpoint[0]), 256, *f.PeerEndpoint)
	}
	if f.Source != nil {
		out.source = C.zlink_spot_peer_source_t(*f.Source)
	}
	if f.State != nil {
		out.state = C.zlink_spot_peer_state_t(*f.State)
	}
	return out
}

func (f SpotNodeSubjectFilter) toC() C.zlink_spot_node_subject_filter_t {
	var out C.zlink_spot_node_subject_filter_t
	if f.Role != nil {
		out.role = C.zlink_spot_role_t(*f.Role)
	}
	if f.Subject != nil {
		mustCopyFixedCString(unsafe.Pointer(&out.subject[0]), 256, *f.Subject)
	}
	if f.SubjectKind != nil {
		out.subject_kind = C.uint32_t(*f.SubjectKind)
	}
	return out
}

func (f SpotNodeSocketSnapshotFilter) toC() C.zlink_spot_node_socket_snapshot_filter_t {
	var out C.zlink_spot_node_socket_snapshot_filter_t
	out.owner = C.zlink_spot_node_socket_owner_t(f.Owner)
	out.socket_type = C.zlink_socket_type_t(f.SocketType)
	if f.SocketName != "" {
		mustCopyFixedCString(unsafe.Pointer(&out.socket_name[0]), 64, f.SocketName)
	}
	return out
}

func mustCopyFixedCString(ptr unsafe.Pointer, size int, value string) {
	if ptr == nil || size <= 0 || len(value) == 0 {
		return
	}
	if len(value) > size-1 {
		panic(validationError("string length %d exceeds fixed field size %d", len(value), size-1))
	}
	if strings.IndexByte(value, 0) >= 0 {
		panic(validationError("string contains null byte"))
	}
	buf := unsafe.Slice((*byte)(ptr), size)
	copy(buf[:len(value)], value)
}

func spotNodeStatusFromC(raw C.zlink_spot_node_status_t) *SpotNodeStatus {
	return &SpotNodeStatus{
		ServiceName:                   C.GoString(&raw.service_name[0]),
		LocalEndpoint:                 C.GoString(&raw.local_endpoint[0]),
		NodeRoutingID:                 routingIDFromC(raw.node_routing_id),
		State:                         SpotNodeState(raw.state),
		ConfiguredPeerCount:           uint32(raw.configured_peer_count),
		ActivePeerCount:               uint32(raw.active_peer_count),
		ConnectedPeerCount:            uint32(raw.connected_peer_count),
		SubjectCount:                  uint32(raw.subject_count),
		ReadySubjectCount:             uint32(raw.ready_subject_count),
		DisconnectedSubTargetCount:    uint32(raw.disconnected_sub_target_count),
		DisconnectedRoutedTargetCount: uint32(raw.disconnected_routed_target_count),
		LastError:                     int32(raw.last_error),
		LastChangedMs:                 uint64(raw.last_changed_ms),
	}
}

func spotNodePeerEntryFromC(raw C.zlink_spot_node_peer_entry_t) SpotNodePeerEntry {
	return SpotNodePeerEntry{
		ServiceName:      C.GoString(&raw.service_name[0]),
		LocalEndpoint:    C.GoString(&raw.local_endpoint[0]),
		PeerEndpoint:     C.GoString(&raw.peer_endpoint[0]),
		Source:           SpotPeerSource(raw.source),
		State:            SpotPeerState(raw.state),
		Weight:           uint32(raw.weight),
		ConnectedSinceMs: uint64(raw.connected_since_ms),
		LastChangedMs:    uint64(raw.last_changed_ms),
	}
}

func spotNodeSubjectEntryFromC(raw C.zlink_spot_node_subject_entry_t) SpotNodeSubjectEntry {
	return SpotNodeSubjectEntry{
		Role:            SpotRole(raw.role),
		Subject:         C.GoString(&raw.subject[0]),
		SubjectKind:     SubjectKind(raw.subject_kind),
		ReadyPeerCount:  uint32(raw.ready_peer_count),
		ActivePeerCount: uint32(raw.active_peer_count),
		LastChangedMs:   uint64(raw.last_changed_ms),
	}
}

func spotNodeSocketSnapshotEntryFromC(raw C.zlink_spot_node_socket_snapshot_entry_t) SpotNodeSocketSnapshotEntry {
	return SpotNodeSocketSnapshotEntry{
		Owner:          SpotNodeSocketOwner(raw.owner),
		OwnerID:        uint64(raw.owner_id),
		OwnerName:      C.GoString(&raw.owner_name[0]),
		SocketName:     C.GoString(&raw.socket_name[0]),
		SocketType:     SocketType(raw.socket_type),
		AutoHwmVisible: uint32(raw.auto_hwm_visible) != 0,
		Snapshot:       monitorSnapshotFromC(raw.snapshot),
	}
}

func registryStatusFromC(raw C.zlink_registry_status_t) *RegistryStatus {
	return &RegistryStatus{
		RegistryID:                 uint32(raw.registry_id),
		BindEndpoint:               C.GoString(&raw.bind_endpoint[0]),
		State:                      RegistryState(raw.state),
		TopologyEntryCount:         uint32(raw.topology_entry_count),
		PeerRegistryCount:          uint32(raw.peer_registry_count),
		ConnectedPeerRegistryCount: uint32(raw.connected_peer_registry_count),
		ListSeq:                    uint64(raw.list_seq),
		LastError:                  int32(raw.last_error),
		LastChangedMs:              uint64(raw.last_changed_ms),
	}
}

func registryServiceSummaryEntryFromC(raw C.zlink_registry_service_summary_entry_t) RegistryServiceSummaryEntry {
	return RegistryServiceSummaryEntry{
		AutoConnectType: AutoConnectType(raw.auto_connect_type),
		ServiceRole:     ServiceRole(raw.service_role),
		ChannelName:     C.GoString(&raw.channel_name[0]),
		TotalCount:      uint32(raw.total_count),
		ConnectingCount: uint32(raw.connecting_count),
		ReadyCount:      uint32(raw.ready_count),
		ErrorCount:      uint32(raw.error_count),
		StoppedCount:    uint32(raw.stopped_count),
		LastReportedMs:  uint64(raw.last_reported_ms),
	}
}

func memberPeerEntryFromC(raw C.zlink_member_peer_entry_t) MemberPeerEntry {
	return MemberPeerEntry{
		AutoConnectType: AutoConnectType(raw.auto_connect_type),
		ServiceRole:     ServiceRole(raw.service_role),
		ChannelName:     C.GoString(&raw.channel_name[0]),
		Endpoint:        C.GoString(&raw.endpoint[0]),
		RoutingID:       routingIDFromC(raw.routing_id),
		Weight:          uint32(raw.weight),
		Value:           int64(raw.value),
	}
}

func registryTopologyEntryFromC(raw C.zlink_registry_topology_entry_t) RegistryTopologyEntry {
	return RegistryTopologyEntry{
		AutoConnectType: AutoConnectType(raw.auto_connect_type),
		RoutingID:       routingIDFromC(raw.routing_id),
		ServiceKind:     ServiceKind(raw.service_kind),
		ServiceRole:     ServiceRole(raw.service_role),
		ChannelName:     C.GoString(&raw.channel_name[0]),
		Endpoint:        C.GoString(&raw.endpoint[0]),
		Source:          TopologySource(raw.source),
		State:           TopologyState(raw.state),
		DesiredCount:    uint32(raw.desired_count),
		ReadyCount:      uint32(raw.ready_count),
		ErrorCode:       uint32(raw.error_code),
		LastReportedMs:  uint64(raw.last_reported_ms),
	}
}

func withDiscoveryCString(d *Discovery, value string, fn func(*C.char) error) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func checkRC(result any) error {
	code := resultCodeInt(result)
	if code == 0 {
		return nil
	}
	switch {
	case code >= 100 && code < 200:
		return requestErrorFromResult(RequestResult(code))
	case code >= 200 && code < 300:
		return recvErrorFromResult(RecvResult(code))
	case code >= 300 && code < 400:
		return handlerErrorFromResult(HandlerResult(code))
	case code >= 400 && code < 500:
		return closeErrorFromResult(CloseResult(code))
	case code >= 500 && code < 600:
		return bindErrorFromResult(BindResult(code))
	case code >= 600 && code < 700:
		return connectErrorFromResult(ConnectResult(code))
	case code >= 700 && code < 800:
		return configErrorFromResult(ConfigResult(code))
	default:
		return configErrorFromErrno(currentErrno())
	}
}

func lastError() error {
	return configErrorFromErrno(currentErrno())
}

func isNativeErrorCode(err error, code int) bool {
	return errors.Is(err, syscall.Errno(code))
}

func withRegistryCString(r *Registry, value string, fn func(*C.char) error) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func withRegistryQueryCString(c *RegistryQueryClient, value string, fn func(*C.char) error) error {
	if c == nil || c.closed {
		return stateError("registry query client is closed")
	}
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func validateFixedCString(name string, value string) error {
	if strings.IndexByte(value, 0) >= 0 {
		return validationError("string contains null byte")
	}
	if len(value) > maxFixedCStringFieldSize {
		return validationError("%s length %d exceeds %d", name, len(value), maxFixedCStringFieldSize)
	}
	return nil
}

func validateEndpointString(value string) error {
	return validateFixedCString("endpoint", value)
}

func validateServiceName(value string) error {
	return validateFixedCString("service_name", value)
}

func validateChannelName(value string) error {
	return validateFixedCString("channel_name", value)
}

func validateSpotNodeSubjectFilter(filter SpotNodeSubjectFilter) error {
	if filter.Subject == nil {
		return nil
	}
	return validateFixedCString("subject", *filter.Subject)
}

func validateSpotNodePeerFilter(filter SpotNodePeerFilter) error {
	if filter.PeerEndpoint == nil {
		return nil
	}
	return validateFixedCString("peer_endpoint", *filter.PeerEndpoint)
}

func validateSpotNodeSocketSnapshotFilter(filter SpotNodeSocketSnapshotFilter) error {
	if filter.SocketName == "" {
		return nil
	}
	if strings.IndexByte(filter.SocketName, 0) >= 0 {
		return validationError("socket_name contains null byte")
	}
	if len(filter.SocketName) > 63 {
		return validationError("socket_name length %d exceeds 63", len(filter.SocketName))
	}
	return nil
}

func validateRegistryServiceSummaryFilter(filter RegistryServiceSummaryFilter) error {
	if filter.ChannelName == nil {
		return nil
	}
	return validateFixedCString("channel_name", *filter.ChannelName)
}

func validateRegistryTopologyFilter(filter RegistryTopologyFilter) error {
	if filter.ChannelName != nil {
		if err := validateFixedCString("channel_name", *filter.ChannelName); err != nil {
			return err
		}
	}
	if filter.RoutingID != nil && filter.RoutingID.Size() == 0 {
		return validationError("routing_id is empty")
	}
	return nil
}
