/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
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
        segment.set(ValueLayout.JAVA_INT, 256, source == null ? 0 : source.getValue());
        segment.set(ValueLayout.JAVA_INT, 260, state == null ? 0 : state.getValue());
        return segment;
    }
}
