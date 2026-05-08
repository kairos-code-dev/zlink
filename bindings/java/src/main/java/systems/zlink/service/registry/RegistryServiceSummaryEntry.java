/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.registry;

import systems.zlink.internal.EnumCodecs;
import systems.zlink.internal.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryServiceSummaryEntry(AutoConnectType autoConnectType,
                                          ServiceRole serviceRole,
                                          String channelName, int totalCount,
                                          int connectingCount, int readyCount,
                                          int errorCount, int stoppedCount,
                                          long lastReportedMs) {
    static RegistryServiceSummaryEntry fromNative(MemorySegment segment) {
        return new RegistryServiceSummaryEntry(
          EnumCodecs.autoConnectTypeFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          EnumCodecs.serviceRoleFromValue(segment.get(ValueLayout.JAVA_INT, 4)),
          NativeHelpers.fromCString(segment.asSlice(8, 256), 256),
          segment.get(ValueLayout.JAVA_INT, 264),
          segment.get(ValueLayout.JAVA_INT, 268),
          segment.get(ValueLayout.JAVA_INT, 272),
          segment.get(ValueLayout.JAVA_INT, 276),
          segment.get(ValueLayout.JAVA_INT, 280),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 288));
    }
}
