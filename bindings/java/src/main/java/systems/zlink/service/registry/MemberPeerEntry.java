/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.registry;

import systems.zlink.RoutingId;
import java.lang.foreign.MemorySegment;

public record MemberPeerEntry(AutoConnectType autoConnectType,
                              ServiceRole serviceRole,
                              String channelName, String endpoint,
                              RoutingId routingId, long value,
                              int weight) {
    static MemberPeerEntry fromNative(MemorySegment segment) {
        return systems.zlink.internal.ServiceDecoders.memberPeerEntry(
          segment);
    }
}
