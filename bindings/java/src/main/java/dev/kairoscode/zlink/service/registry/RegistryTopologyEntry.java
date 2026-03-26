/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.ServiceKind;
import dev.kairoscode.zlink.ServiceRole;
import dev.kairoscode.zlink.internal.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryTopologyEntry(RoutingId routingId,
                                    ServiceKind serviceKind,
                                    ServiceRole serviceRole,
                                    String serviceName, String endpoint,
                                    int source, int state, int desiredCount,
                                    int readyCount, int errorCode,
                                    long lastReportedMs) {
    static RegistryTopologyEntry fromNative(MemorySegment segment) {
        int routingSize = segment.get(ValueLayout.JAVA_BYTE, 0) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment, 1, MemorySegment.ofArray(routing), 0,
              routingSize);
        }
        return new RegistryTopologyEntry(
          RoutingId.copyOf(routing),
          ServiceKind.fromValue(segment.get(ValueLayout.JAVA_INT, 256)),
          ServiceRole.fromValue(segment.get(ValueLayout.JAVA_INT, 260)),
          NativeHelpers.fromCString(segment.asSlice(264, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(520, 256), 256),
          segment.get(ValueLayout.JAVA_INT, 776),
          segment.get(ValueLayout.JAVA_INT, 780),
          segment.get(ValueLayout.JAVA_INT, 784),
          segment.get(ValueLayout.JAVA_INT, 788),
          segment.get(ValueLayout.JAVA_INT, 792),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 800));
    }
}
