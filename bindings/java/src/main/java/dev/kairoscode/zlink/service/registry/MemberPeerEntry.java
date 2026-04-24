/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.RoutingId;
import java.lang.foreign.MemorySegment;

public record MemberPeerEntry(ServiceType serviceType, ServiceRole serviceRole,
                              String serviceName, String endpoint,
                              RoutingId routingId, long value,
                              int weight) {
    static MemberPeerEntry fromNative(MemorySegment segment) {
        return dev.kairoscode.zlink.internal.ServiceDecoders.memberPeerEntry(
          segment);
    }
}
