/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;


import systems.zlink.runtime.nativebridge.EnumCodecs;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryStatus(int registryId, String bindEndpoint, RegistryState state,
                             int topologyEntryCount, int peerRegistryCount,
                             int connectedPeerRegistryCount, long listSeq,
                             int lastError, long lastChangedMs) {
    static RegistryStatus fromNative(MemorySegment segment) {
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
}
