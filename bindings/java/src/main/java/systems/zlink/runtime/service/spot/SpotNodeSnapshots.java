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
        int routingSize = segment.get(ValueLayout.JAVA_BYTE, 512) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment, 513, MemorySegment.ofArray(routing), 0,
              routingSize);
        }
        return new SpotNodeStatus(
          NativeHelpers.fromCString(segment.asSlice(0, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(256, 256), 256),
          RoutingId.from(routing),
          EnumCodecs.spotNodeStateFromValue(segment.get(ValueLayout.JAVA_INT, 768)),
          segment.get(ValueLayout.JAVA_INT, 772),
          segment.get(ValueLayout.JAVA_INT, 776),
          segment.get(ValueLayout.JAVA_INT, 780),
          segment.get(ValueLayout.JAVA_INT, 784),
          segment.get(ValueLayout.JAVA_INT, 788),
          segment.get(ValueLayout.JAVA_INT, 792),
          segment.get(ValueLayout.JAVA_INT, 796),
          segment.get(ValueLayout.JAVA_INT, 800),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 808));
    }

    static MemorySegment subjectFilterToNative(SpotNodeSubjectFilter filter,
                                               Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SUBJECT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          filter.role() == null ? 0 : EnumCodecs.spotRoleValue(filter.role()));
        if (filter.subject() != null && !filter.subject().isEmpty()) {
            MemorySegment nativeSubject =
              NativeHelpers.toCString(arena, filter.subject());
            MemorySegment.copy(nativeSubject, 0, segment, 4,
              Math.min(nativeSubject.byteSize(), 256));
        }
        segment.set(ValueLayout.JAVA_INT, 260,
          filter.subjectKind() == null ? 0
            : EnumCodecs.serviceEventSubjectKindValue(filter.subjectKind()));
        return segment;
    }

    static SpotNodeSubjectEntry subjectEntryFromNative(MemorySegment segment) {
        return new SpotNodeSubjectEntry(
          EnumCodecs.spotRoleFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          NativeHelpers.fromCString(segment.asSlice(4, 256), 256),
          EnumCodecs.serviceEventSubjectKindFromValue(
              segment.get(ValueLayout.JAVA_INT, 260)),
          segment.get(ValueLayout.JAVA_INT, 264),
          segment.get(ValueLayout.JAVA_INT, 268),
          segment.get(ValueLayout.JAVA_LONG, 272));
    }

    static MemorySegment socketFilterToNative(SpotNodeSocketFilter filter,
                                              Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          filter.owner() == null ? 0
            : EnumCodecs.spotNodeSocketOwnerValue(filter.owner()));
        segment.set(ValueLayout.JAVA_INT, 4,
          filter.socketType() == null ? 0
            : EnumCodecs.socketTypeValue(filter.socketType()));
        if (filter.socketName() != null && !filter.socketName().isEmpty()) {
            MemorySegment nativeName =
              NativeHelpers.toCString(arena, filter.socketName());
            MemorySegment.copy(nativeName, 0, segment, 8,
              Math.min(nativeName.byteSize(), 64));
        }
        return segment;
    }

    static SpotNodeSocketEntry socketEntryFromNative(MemorySegment segment) {
        return new SpotNodeSocketEntry(
          EnumCodecs.spotNodeSocketOwnerFromValue(
              segment.get(ValueLayout.JAVA_INT, 0)),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 8),
          NativeHelpers.fromCString(segment.asSlice(16, 64), 64),
          NativeHelpers.fromCString(segment.asSlice(80, 64), 64),
          EnumCodecs.socketTypeFromValue(segment.get(ValueLayout.JAVA_INT, 144)),
          segment.get(ValueLayout.JAVA_INT, 148) != 0,
          NativeMonitorStatuses.fromNative(segment.asSlice(152,
            NativeLayouts.MONITOR_SNAPSHOT_LAYOUT.byteSize())));
    }

    static MemorySegment peerFilterToNative(SpotNodePeerFilter filter,
                                            Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_PEER_FILTER_LAYOUT);
        if (filter.peerEndpoint() != null && !filter.peerEndpoint().isEmpty()) {
            MemorySegment endpoint =
              NativeHelpers.toCString(arena, filter.peerEndpoint());
            MemorySegment.copy(endpoint, 0, segment, 0,
              Math.min(endpoint.byteSize(), 256));
        }
        segment.set(ValueLayout.JAVA_INT, 256,
          filter.source() == null ? 0
            : EnumCodecs.spotPeerSourceValue(filter.source()));
        segment.set(ValueLayout.JAVA_INT, 260,
          filter.state() == null ? 0
            : EnumCodecs.spotPeerStateValue(filter.state()));
        return segment;
    }

    static SpotNodePeerEntry peerEntryFromNative(MemorySegment segment) {
        return new SpotNodePeerEntry(
          NativeHelpers.fromCString(segment.asSlice(0, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(256, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(512, 256), 256),
          EnumCodecs.spotPeerSourceFromValue(segment.get(ValueLayout.JAVA_INT, 768)),
          EnumCodecs.spotPeerKindFromValue(segment.get(ValueLayout.JAVA_INT, 772)),
          EnumCodecs.spotPeerStateFromValue(segment.get(ValueLayout.JAVA_INT, 776)),
          segment.get(ValueLayout.JAVA_INT, 780),
          segment.get(ValueLayout.JAVA_LONG, 784),
          segment.get(ValueLayout.JAVA_LONG, 792));
    }

    static int boundedCount(long value) {
        if (value <= 0)
            return 0;
        if (value > Integer.MAX_VALUE)
            return Integer.MAX_VALUE;
        return (int) value;
    }
}
