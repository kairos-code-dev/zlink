/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodePeerFilter;
import systems.zlink.contracts.service.spot.SpotNodeSocketEntry;
import systems.zlink.contracts.service.spot.SpotNodeSocketFilter;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.service.spot.SpotNodeSubjectFilter;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMonitorStatuses;

final class SpotNodeSnapshots {
    private SpotNodeSnapshots() {
    }

    static SpotNodeStatus statusFromNative(MemorySegment segment) {
        int routingSize = segment.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.SPOT_NODE_STATUS_NODE_ROUTING_ID_OFFSET
            + NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment,
              NativeLayouts.SPOT_NODE_STATUS_NODE_ROUTING_ID_OFFSET
                + NativeLayouts.ROUTING_ID_DATA_OFFSET,
              MemorySegment.ofArray(routing), 0, routingSize);
        }
        return new SpotNodeStatus(
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.SPOT_NODE_STATUS_CHANNEL_NAME_OFFSET, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.SPOT_NODE_STATUS_LOCAL_ENDPOINT_OFFSET, 256), 256),
          RoutingId.from(routing),
          EnumCodecs.spotNodeStateFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_STATE_OFFSET)),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_CONFIGURED_PEER_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_ACTIVE_PEER_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_CONNECTED_PEER_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_SUBJECT_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_READY_SUBJECT_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_DISCONNECTED_SUB_TARGET_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_DISCONNECTED_ROUTED_TARGET_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_STATUS_LAST_ERROR_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.SPOT_NODE_STATUS_LAST_CHANGED_MS_OFFSET));
    }

    static MemorySegment subjectFilterToNative(SpotNodeSubjectFilter filter,
                                               Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SUBJECT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.SPOT_NODE_SUBJECT_FILTER_ROLE_OFFSET,
          filter.role() == null ? 0 : EnumCodecs.spotRoleValue(filter.role()));
        if (filter.subject() != null && !filter.subject().isEmpty()) {
            MemorySegment nativeSubject =
              NativeHelpers.toCString(arena, filter.subject());
            MemorySegment.copy(nativeSubject, 0, segment,
              NativeLayouts.SPOT_NODE_SUBJECT_FILTER_SUBJECT_OFFSET,
              Math.min(nativeSubject.byteSize(), 256));
        }
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.SPOT_NODE_SUBJECT_FILTER_SUBJECT_KIND_OFFSET,
          filter.subjectKind() == null ? 0
            : EnumCodecs.serviceEventSubjectKindValue(filter.subjectKind()));
        return segment;
    }

    static SpotNodeSubjectEntry subjectEntryFromNative(MemorySegment segment) {
        return new SpotNodeSubjectEntry(
          EnumCodecs.spotRoleFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_ROLE_OFFSET)),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_SUBJECT_OFFSET, 256), 256),
          EnumCodecs.serviceEventSubjectKindFromValue(
              segment.get(ValueLayout.JAVA_INT,
                NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_SUBJECT_KIND_OFFSET)),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_READY_PEER_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_ACTIVE_PEER_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_LAST_CHANGED_MS_OFFSET));
    }

    static MemorySegment socketFilterToNative(SpotNodeSocketFilter filter,
                                              Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.SPOT_NODE_SOCKET_FILTER_OWNER_OFFSET,
          filter.owner() == null ? 0
            : EnumCodecs.spotNodeSocketOwnerValue(filter.owner()));
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.SPOT_NODE_SOCKET_FILTER_SOCKET_TYPE_OFFSET,
          filter.socketType() == null ? 0
            : EnumCodecs.socketTypeValue(filter.socketType()));
        if (filter.socketName() != null && !filter.socketName().isEmpty()) {
            MemorySegment nativeName =
              NativeHelpers.toCString(arena, filter.socketName());
            MemorySegment.copy(nativeName, 0, segment,
              NativeLayouts.SPOT_NODE_SOCKET_FILTER_SOCKET_NAME_OFFSET,
              Math.min(nativeName.byteSize(), 64));
        }
        return segment;
    }

    static SpotNodeSocketEntry socketEntryFromNative(MemorySegment segment) {
        return new SpotNodeSocketEntry(
          EnumCodecs.spotNodeSocketOwnerFromValue(
              segment.get(ValueLayout.JAVA_INT,
                NativeLayouts.SPOT_NODE_SOCKET_ENTRY_OWNER_OFFSET)),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.SPOT_NODE_SOCKET_ENTRY_OWNER_ID_OFFSET),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.SPOT_NODE_SOCKET_ENTRY_OWNER_NAME_OFFSET, 64), 64),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.SPOT_NODE_SOCKET_ENTRY_SOCKET_NAME_OFFSET, 64), 64),
          EnumCodecs.socketTypeFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SOCKET_ENTRY_SOCKET_TYPE_OFFSET)),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_SOCKET_ENTRY_AUTO_HWM_VISIBLE_OFFSET) != 0,
          NativeMonitorStatuses.fromNative(segment.asSlice(
            NativeLayouts.SPOT_NODE_SOCKET_ENTRY_SNAPSHOT_OFFSET,
            NativeLayouts.MONITOR_SNAPSHOT_LAYOUT.byteSize())));
    }

    static MemorySegment peerFilterToNative(SpotNodePeerFilter filter,
                                            Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_PEER_FILTER_LAYOUT);
        if (filter.peerEndpoint() != null && !filter.peerEndpoint().isEmpty()) {
            MemorySegment endpoint =
              NativeHelpers.toCString(arena, filter.peerEndpoint());
            MemorySegment.copy(endpoint, 0, segment,
              NativeLayouts.SPOT_NODE_PEER_FILTER_PEER_ENDPOINT_OFFSET,
              Math.min(endpoint.byteSize(), 256));
        }
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.SPOT_NODE_PEER_FILTER_SOURCE_OFFSET,
          filter.source() == null ? 0
            : EnumCodecs.spotPeerSourceValue(filter.source()));
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.SPOT_NODE_PEER_FILTER_STATE_OFFSET,
          filter.state() == null ? 0
            : EnumCodecs.spotPeerStateValue(filter.state()));
        return segment;
    }

    static SpotNodePeerEntry peerEntryFromNative(MemorySegment segment) {
        return new SpotNodePeerEntry(
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.SPOT_NODE_PEER_ENTRY_CHANNEL_NAME_OFFSET, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.SPOT_NODE_PEER_ENTRY_LOCAL_ENDPOINT_OFFSET, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.SPOT_NODE_PEER_ENTRY_PEER_ENDPOINT_OFFSET, 256), 256),
          EnumCodecs.spotPeerSourceFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_PEER_ENTRY_SOURCE_OFFSET)),
          EnumCodecs.spotPeerKindFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_PEER_ENTRY_KIND_OFFSET)),
          EnumCodecs.spotPeerStateFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_PEER_ENTRY_STATE_OFFSET)),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_NODE_PEER_ENTRY_WEIGHT_OFFSET),
          segment.get(ValueLayout.JAVA_LONG,
            NativeLayouts.SPOT_NODE_PEER_ENTRY_CONNECTED_SINCE_MS_OFFSET),
          segment.get(ValueLayout.JAVA_LONG,
            NativeLayouts.SPOT_NODE_PEER_ENTRY_LAST_CHANGED_MS_OFFSET));
    }

}
