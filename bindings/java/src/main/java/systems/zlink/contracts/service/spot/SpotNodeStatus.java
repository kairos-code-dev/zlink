/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.RoutingId;
import systems.zlink.runtime.nativebridge.EnumCodecs;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodeStatus(String channelName, String localEndpoint,
                             RoutingId nodeRoutingId, SpotNodeState state,
                             int configuredPeerCount, int activePeerCount,
                             int connectedPeerCount, int subjectCount,
                             int readySubjectCount,
                             int disconnectedSubTargetCount,
                             int disconnectedRoutedTargetCount, int lastError,
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
          RoutingId.fromBytes(routing),
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
}
