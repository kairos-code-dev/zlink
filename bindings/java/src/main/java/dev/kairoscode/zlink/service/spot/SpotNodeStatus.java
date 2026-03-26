/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.internal.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodeStatus(String serviceName, String localEndpoint,
                             RoutingId nodeRoutingId, int state,
                             int configuredPeerCount, int activePeerCount,
                             int connectedPeerCount, int subjectCount,
                             int readySubjectCount, int lastError,
                             long lastChangedMs) {
    static SpotNodeStatus fromNative(MemorySegment segment) {
        int routingSize = segment.get(ValueLayout.JAVA_BYTE, 512) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment, 513, MemorySegment.ofArray(routing), 0,
              routingSize);
        }
        return new SpotNodeStatus(
          NativeHelpers.fromCString(segment.asSlice(0, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(256, 256), 256),
          RoutingId.copyOf(routing),
          segment.get(ValueLayout.JAVA_INT, 768),
          segment.get(ValueLayout.JAVA_INT, 772),
          segment.get(ValueLayout.JAVA_INT, 776),
          segment.get(ValueLayout.JAVA_INT, 780),
          segment.get(ValueLayout.JAVA_INT, 784),
          segment.get(ValueLayout.JAVA_INT, 788),
          segment.get(ValueLayout.JAVA_INT, 792),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 800));
    }
}
