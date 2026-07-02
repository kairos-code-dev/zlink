// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include "zlink.h"
*/
import "C"

import (
	"strings"
	"unsafe"
)

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
