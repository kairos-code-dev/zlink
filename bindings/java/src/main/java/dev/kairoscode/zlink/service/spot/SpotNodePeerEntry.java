/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.AdmissionState;
import dev.kairoscode.zlink.internal.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodePeerEntry(String serviceName, String localEndpoint,
                                String peerEndpoint, SpotPeerSource source,
                                SpotPeerState state, AdmissionState admissionState,
                                long connectedSinceMs, long lastChangedMs) {
    static SpotNodePeerEntry fromNative(MemorySegment segment) {
        return new SpotNodePeerEntry(
          NativeHelpers.fromCString(segment.asSlice(0, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(256, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(512, 256), 256),
          SpotPeerSource.fromValue(segment.get(ValueLayout.JAVA_INT, 768)),
          SpotPeerState.fromValue(segment.get(ValueLayout.JAVA_INT, 772)),
          AdmissionState.fromValue(segment.get(ValueLayout.JAVA_INT, 776)),
          segment.get(ValueLayout.JAVA_LONG, 784),
          segment.get(ValueLayout.JAVA_LONG, 792));
    }
}
