/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.RoutingId;
import java.lang.foreign.MemorySegment;

public record MemberPeerEntry(AutoConnectType autoConnectType,
                              ServiceRole serviceRole,
                              String channelName, String endpoint,
                              RoutingId routingId, long value,
                              int weight) {
    static MemberPeerEntry fromNative(MemorySegment segment) {
        return systems.zlink.runtime.nativebridge.ServiceDecoders.memberPeerEntry(
          segment);
    }
}
