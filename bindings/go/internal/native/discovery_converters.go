// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import (
	"strings"
	"unsafe"
)

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

func (f SpotNodeSocketFilter) toC() C.zlink_spot_node_socket_filter_t {
	var out C.zlink_spot_node_socket_filter_t
	if f.Owner != nil {
		out.owner = C.zlink_spot_node_socket_owner_t(*f.Owner)
	}
	if f.SocketType != nil {
		out.socket_type = C.zlink_socket_type_t(*f.SocketType)
	}
	if f.SocketName != nil && *f.SocketName != "" {
		mustCopyFixedCString(unsafe.Pointer(&out.socket_name[0]), 64, *f.SocketName)
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
		ChannelName:                   C.GoString(&raw.channel_name[0]),
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
		ChannelName:      C.GoString(&raw.channel_name[0]),
		LocalEndpoint:    C.GoString(&raw.local_endpoint[0]),
		PeerEndpoint:     C.GoString(&raw.peer_endpoint[0]),
		Source:           SpotPeerSource(raw.source),
		Kind:             SpotPeerKind(raw.kind),
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

func spotNodeSocketEntryFromC(raw C.zlink_spot_node_socket_entry_t) SpotNodeSocketEntry {
	return SpotNodeSocketEntry{
		Owner:          SpotNodeSocketOwner(raw.owner),
		OwnerID:        uint64(raw.owner_id),
		OwnerName:      C.GoString(&raw.owner_name[0]),
		SocketName:     C.GoString(&raw.socket_name[0]),
		SocketType:     SocketType(raw.socket_type),
		AutoHwmVisible: uint32(raw.auto_hwm_visible) != 0,
		MonitorStatus:  monitorStatusFromC(raw.monitor_status),
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
		SpotKind:        SpotKind(raw.spot_kind),
	}
}

func spotRouteFromC(raw C.zlink_spot_route_t) SpotRoute {
	return SpotRoute{
		SpotRID:      routingIDFromC(raw.spot_rid),
		OwnerNodeRID: routingIDFromC(raw.owner_node_rid),
		SpotKind:     SpotKind(raw.spot_kind),
	}
}
