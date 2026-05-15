/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.runtime.nativebridge.EnumCodecs;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import systems.zlink.contracts.service.registry.ServiceEventSubjectKind;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodeSubjectEntry(SpotRole role, String subject,
                                   ServiceEventSubjectKind subjectKind,
                                   int readyPeerCount, int activePeerCount,
                                   long lastChangedMs) {
    static SpotNodeSubjectEntry fromNative(MemorySegment segment) {
        return new SpotNodeSubjectEntry(
          EnumCodecs.spotRoleFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          NativeHelpers.fromCString(segment.asSlice(4, 256), 256),
          EnumCodecs.serviceEventSubjectKindFromValue(
              segment.get(ValueLayout.JAVA_INT, 260)),
          segment.get(ValueLayout.JAVA_INT, 264),
          segment.get(ValueLayout.JAVA_INT, 268),
          segment.get(ValueLayout.JAVA_LONG, 272));
    }
}
