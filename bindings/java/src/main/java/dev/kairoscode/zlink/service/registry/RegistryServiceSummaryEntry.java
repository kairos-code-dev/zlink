/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.ServiceKind;
import dev.kairoscode.zlink.ServiceRole;
import dev.kairoscode.zlink.internal.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryServiceSummaryEntry(ServiceKind serviceKind,
                                          ServiceRole serviceRole,
                                          String serviceName, int totalCount,
                                          int connectingCount, int readyCount,
                                          int errorCount, int stoppedCount,
                                          long lastReportedMs) {
    static RegistryServiceSummaryEntry fromNative(MemorySegment segment) {
        return new RegistryServiceSummaryEntry(
          ServiceKind.fromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          ServiceRole.fromValue(segment.get(ValueLayout.JAVA_INT, 4)),
          NativeHelpers.fromCString(segment.asSlice(8, 256), 256),
          segment.get(ValueLayout.JAVA_INT, 264),
          segment.get(ValueLayout.JAVA_INT, 268),
          segment.get(ValueLayout.JAVA_INT, 272),
          segment.get(ValueLayout.JAVA_INT, 276),
          segment.get(ValueLayout.JAVA_INT, 280),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 288));
    }
}
