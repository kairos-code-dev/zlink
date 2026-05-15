/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import systems.zlink.runtime.nativebridge.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodePeerFilter(String peerEndpoint, SpotPeerSource source,
                                 SpotPeerState state) {
    MemorySegment toNative(Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_PEER_FILTER_LAYOUT);
        if (peerEndpoint != null && !peerEndpoint.isEmpty()) {
            MemorySegment endpoint = NativeHelpers.toCString(arena, peerEndpoint);
            MemorySegment.copy(endpoint, 0, segment, 0,
              Math.min(endpoint.byteSize(), 256));
        }
        segment.set(ValueLayout.JAVA_INT, 256,
          source == null ? 0 : EnumCodecs.spotPeerSourceValue(source));
        segment.set(ValueLayout.JAVA_INT, 260,
          state == null ? 0 : EnumCodecs.spotPeerStateValue(state));
        return segment;
    }
}
