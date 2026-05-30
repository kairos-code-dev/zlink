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
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.REGISTRY_TOPOLOGY_FILTER_AUTO_CONNECT_TYPE_OFFSET,
          autoConnectTypeValue(filter.autoConnectType()));
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.REGISTRY_TOPOLOGY_FILTER_SERVICE_KIND_OFFSET,
          serviceKindValue(filter.serviceKind()));
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.REGISTRY_TOPOLOGY_FILTER_SERVICE_ROLE_OFFSET,
          serviceRoleValue(filter.serviceRole()));
        String channelName = filter.channelName();
        if (channelName != null && !channelName.isEmpty()) {
            MemorySegment name = NativeHelpers.toCString(arena, channelName);
            MemorySegment.copy(name, 0, segment,
              NativeLayouts.REGISTRY_TOPOLOGY_FILTER_CHANNEL_NAME_OFFSET,
              Math.min(name.byteSize(), 256));
        }
        RoutingId routingId = filter.routingId();
        if (routingId != null) {
            byte[] bytes = InternalAccess.routingIdTrustedBytes(routingId);
            segment.set(ValueLayout.JAVA_BYTE,
              NativeLayouts.REGISTRY_TOPOLOGY_FILTER_ROUTING_ID_OFFSET
                + NativeLayouts.ROUTING_ID_SIZE_OFFSET, (byte) bytes.length);
            if (bytes.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(bytes), 0, segment,
                  NativeLayouts.REGISTRY_TOPOLOGY_FILTER_ROUTING_ID_OFFSET
                    + NativeLayouts.ROUTING_ID_DATA_OFFSET, bytes.length);
            }
        }
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.REGISTRY_TOPOLOGY_FILTER_STATE_OFFSET,
          topologyStateValue(filter.state()));
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.REGISTRY_TOPOLOGY_FILTER_SOURCE_OFFSET,
          topologySourceValue(filter.source()));
        return segment;
    }

    static MemorySegment serviceSummaryFilterToNative(
      RegistryServiceSummaryFilter filter, Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.REGISTRY_SERVICE_SUMMARY_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.REGISTRY_SERVICE_SUMMARY_FILTER_AUTO_CONNECT_TYPE_OFFSET,
          autoConnectTypeValue(filter.autoConnectType()));
        segment.set(ValueLayout.JAVA_INT,
          NativeLayouts.REGISTRY_SERVICE_SUMMARY_FILTER_SERVICE_ROLE_OFFSET,
          serviceRoleValue(filter.serviceRole()));
        String channelName = filter.channelName();
        if (channelName != null && !channelName.isEmpty()) {
            MemorySegment name = NativeHelpers.toCString(arena, channelName);
            MemorySegment.copy(name, 0, segment,
              NativeLayouts.REGISTRY_SERVICE_SUMMARY_FILTER_CHANNEL_NAME_OFFSET,
              Math.min(name.byteSize(), 256));
        }
        return segment;
    }

    static RegistryStatus statusFromNative(MemorySegment segment) {
        return new RegistryStatus(
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_STATUS_REGISTRY_ID_OFFSET),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.REGISTRY_STATUS_BIND_ENDPOINT_OFFSET, 256), 256),
          EnumCodecs.registryStateFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_STATUS_STATE_OFFSET)),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_STATUS_TOPOLOGY_ENTRY_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_STATUS_PEER_REGISTRY_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_STATUS_CONNECTED_PEER_REGISTRY_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.REGISTRY_STATUS_LIST_SEQ_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_STATUS_LAST_ERROR_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.REGISTRY_STATUS_LAST_CHANGED_MS_OFFSET));
    }

    public static RegistryTopologyEntry topologyEntryFromNative(
      MemorySegment segment) {
        int routingSize = segment.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_ROUTING_ID_OFFSET
            + NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment,
              NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_ROUTING_ID_OFFSET
                + NativeLayouts.ROUTING_ID_DATA_OFFSET,
              MemorySegment.ofArray(routing), 0, routingSize);
        }
        return new RegistryTopologyEntry(
          EnumCodecs.autoConnectTypeFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_AUTO_CONNECT_TYPE_OFFSET)),
          InternalAccess.routingIdFromTrusted(routing),
          EnumCodecs.serviceKindFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_SERVICE_KIND_OFFSET)),
          EnumCodecs.serviceRoleFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_SERVICE_ROLE_OFFSET)),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_CHANNEL_NAME_OFFSET, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_ENDPOINT_OFFSET, 256), 256),
          EnumCodecs.topologySourceFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_SOURCE_OFFSET)),
          EnumCodecs.topologyStateFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_STATE_OFFSET)),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_DESIRED_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_READY_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_ERROR_CODE_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_LAST_REPORTED_MS_OFFSET),
          EnumCodecs.spotKindFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_SPOT_KIND_OFFSET)));
    }

    public static RegistryServiceSummaryEntry serviceSummaryEntryFromNative(
      MemorySegment segment) {
        return new RegistryServiceSummaryEntry(
          EnumCodecs.autoConnectTypeFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_AUTO_CONNECT_TYPE_OFFSET)),
          EnumCodecs.serviceRoleFromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_SERVICE_ROLE_OFFSET)),
          NativeHelpers.fromCString(segment.asSlice(
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_CHANNEL_NAME_OFFSET, 256),
            256),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_TOTAL_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_CONNECTING_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_READY_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_ERROR_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_STOPPED_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_LAST_REPORTED_MS_OFFSET));
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
