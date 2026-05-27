/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotKind;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryTopologyEntry(AutoConnectType autoConnectType,
                                    RoutingId routingId,
                                    ServiceKind serviceKind,
                                    ServiceRole serviceRole,
                                    String channelName, String endpoint,
                                    TopologySource source, TopologyState state,
                                    int desiredCount,
                                    int readyCount, int errorCode,
                                    long lastReportedMs,
                                    SpotKind spotKind) {
    static RegistryTopologyEntry fromNative(MemorySegment segment) {
        int routingSize = segment.get(ValueLayout.JAVA_BYTE, 4) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment, 5, MemorySegment.ofArray(routing), 0,
              routingSize);
        }
        return new RegistryTopologyEntry(
          EnumCodecs.autoConnectTypeFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          RoutingId.from(routing),
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
}
