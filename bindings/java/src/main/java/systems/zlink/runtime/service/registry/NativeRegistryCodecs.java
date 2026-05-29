/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.registry;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.MemberPeerEntry;
import systems.zlink.contracts.service.registry.RegistryServiceSummaryEntry;
import systems.zlink.contracts.service.registry.RegistryServiceSummaryFilter;
import systems.zlink.contracts.service.registry.RegistryStatus;
import systems.zlink.contracts.service.registry.RegistryTopologyEntry;
import systems.zlink.contracts.service.registry.RegistryTopologyFilter;
import systems.zlink.contracts.service.registry.ServiceKind;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.registry.TopologySource;
import systems.zlink.contracts.service.registry.TopologyState;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class NativeRegistryCodecs {
    private NativeRegistryCodecs() {
    }

    static MemorySegment topologyFilterToNative(
      RegistryTopologyFilter filter, Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.REGISTRY_TOPOLOGY_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          autoConnectTypeValue(filter.autoConnectType()));
        segment.set(ValueLayout.JAVA_INT, 4,
          serviceKindValue(filter.serviceKind()));
        segment.set(ValueLayout.JAVA_INT, 8,
          serviceRoleValue(filter.serviceRole()));
        String channelName = filter.channelName();
        if (channelName != null && !channelName.isEmpty()) {
            MemorySegment name = NativeHelpers.toCString(arena, channelName);
            MemorySegment.copy(name, 0, segment, 12,
              Math.min(name.byteSize(), 256));
        }
        RoutingId routingId = filter.routingId();
        if (routingId != null) {
            byte[] bytes = InternalAccess.routingIdTrustedBytes(routingId);
            segment.set(ValueLayout.JAVA_BYTE, 268, (byte) bytes.length);
            if (bytes.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(bytes), 0, segment,
                  269, bytes.length);
            }
        }
        segment.set(ValueLayout.JAVA_INT, 524,
          topologyStateValue(filter.state()));
        segment.set(ValueLayout.JAVA_INT, 528,
          topologySourceValue(filter.source()));
        return segment;
    }

    static MemorySegment serviceSummaryFilterToNative(
      RegistryServiceSummaryFilter filter, Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.REGISTRY_SERVICE_SUMMARY_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          autoConnectTypeValue(filter.autoConnectType()));
        segment.set(ValueLayout.JAVA_INT, 4,
          serviceRoleValue(filter.serviceRole()));
        String channelName = filter.channelName();
        if (channelName != null && !channelName.isEmpty()) {
            MemorySegment name = NativeHelpers.toCString(arena, channelName);
            MemorySegment.copy(name, 0, segment, 8,
              Math.min(name.byteSize(), 256));
        }
        return segment;
    }

    static RegistryStatus statusFromNative(MemorySegment segment) {
        return new RegistryStatus(
          segment.get(ValueLayout.JAVA_INT, 0),
          NativeHelpers.fromCString(segment.asSlice(4, 256), 256),
          EnumCodecs.registryStateFromValue(segment.get(ValueLayout.JAVA_INT, 260)),
          segment.get(ValueLayout.JAVA_INT, 264),
          segment.get(ValueLayout.JAVA_INT, 268),
          segment.get(ValueLayout.JAVA_INT, 272),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 280),
          segment.get(ValueLayout.JAVA_INT, 288),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 296));
    }

    public static RegistryTopologyEntry topologyEntryFromNative(
      MemorySegment segment) {
        int routingSize = segment.get(ValueLayout.JAVA_BYTE, 4) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment, 5, MemorySegment.ofArray(routing), 0,
              routingSize);
        }
        return new RegistryTopologyEntry(
          EnumCodecs.autoConnectTypeFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          InternalAccess.routingIdFromTrusted(routing),
          EnumCodecs.serviceKindFromValue(segment.get(ValueLayout.JAVA_INT, 260)),
          EnumCodecs.serviceRoleFromValue(segment.get(ValueLayout.JAVA_INT, 264)),
          NativeHelpers.fromCString(segment.asSlice(268, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(524, 256), 256),
          EnumCodecs.topologySourceFromValue(segment.get(ValueLayout.JAVA_INT, 780)),
          EnumCodecs.topologyStateFromValue(segment.get(ValueLayout.JAVA_INT, 784)),
          segment.get(ValueLayout.JAVA_INT, 788),
          segment.get(ValueLayout.JAVA_INT, 792),
          segment.get(ValueLayout.JAVA_INT, 796),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 800),
          EnumCodecs.spotKindFromValue(segment.get(ValueLayout.JAVA_INT, 808)));
    }

    public static RegistryServiceSummaryEntry serviceSummaryEntryFromNative(
      MemorySegment segment) {
        return new RegistryServiceSummaryEntry(
          EnumCodecs.autoConnectTypeFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          EnumCodecs.serviceRoleFromValue(segment.get(ValueLayout.JAVA_INT, 4)),
          NativeHelpers.fromCString(segment.asSlice(8, 256), 256),
          segment.get(ValueLayout.JAVA_INT, 264),
          segment.get(ValueLayout.JAVA_INT, 268),
          segment.get(ValueLayout.JAVA_INT, 272),
          segment.get(ValueLayout.JAVA_INT, 276),
          segment.get(ValueLayout.JAVA_INT, 280),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 288));
    }

    public static MemberPeerEntry memberPeerEntryFromNative(
      MemorySegment segment) {
        var autoConnectType = EnumCodecs.autoConnectTypeFromValue(
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MEMBER_PEER_AUTO_CONNECT_TYPE_OFFSET));
        var serviceRole = EnumCodecs.serviceRoleFromValue(
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MEMBER_PEER_SERVICE_ROLE_OFFSET));
        String channelName = NativeHelpers.fromCString(segment.asSlice(
          NativeLayouts.MEMBER_PEER_CHANNEL_NAME_OFFSET, 256), 256);
        String endpoint = NativeHelpers.fromCString(segment.asSlice(
          NativeLayouts.MEMBER_PEER_ENDPOINT_OFFSET, 256), 256);
        int routingSize = segment.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.MEMBER_PEER_ROUTING_ID_OFFSET
            + NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        byte[] routingBytes = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment,
              NativeLayouts.MEMBER_PEER_ROUTING_ID_OFFSET
                + NativeLayouts.ROUTING_ID_DATA_OFFSET,
              MemorySegment.ofArray(routingBytes), 0, routingSize);
        }
        int weight = segment.get(ValueLayout.JAVA_INT,
          NativeLayouts.MEMBER_PEER_WEIGHT_OFFSET);
        long value = segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
          NativeLayouts.MEMBER_PEER_VALUE_OFFSET);
        return new MemberPeerEntry(autoConnectType, serviceRole, channelName,
          endpoint, InternalAccess.routingIdFromTrusted(routingBytes), value,
          weight);
    }

    private static int autoConnectTypeValue(AutoConnectType value) {
        return value == null ? 0 : EnumCodecs.autoConnectTypeValue(value);
    }

    private static int serviceKindValue(ServiceKind value) {
        return value == null ? 0 : EnumCodecs.serviceKindValue(value);
    }

    private static int serviceRoleValue(ServiceRole value) {
        return value == null ? 0 : EnumCodecs.serviceRoleValue(value);
    }

    private static int topologyStateValue(TopologyState value) {
        return value == null ? 0 : EnumCodecs.topologyStateValue(value);
    }

    private static int topologySourceValue(TopologySource value) {
        return value == null ? 0 : EnumCodecs.topologySourceValue(value);
    }
}
