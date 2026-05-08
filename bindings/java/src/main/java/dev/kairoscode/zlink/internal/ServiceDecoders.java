/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.service.registry.MemberPeerEntry;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

/**
 * Internal FFM decoders for service-layer value records.
 */
public final class ServiceDecoders {
    private ServiceDecoders() {
    }

    public static MemberPeerEntry memberPeerEntry(MemorySegment segment) {
        var autoConnectType = EnumCodecs.autoConnectTypeFromValue(
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MEMBER_PEER_AUTO_CONNECT_TYPE_OFFSET));
        var serviceRole = EnumCodecs.serviceRoleFromValue(
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MEMBER_PEER_SERVICE_ROLE_OFFSET));
        String channelName = NativeHelpers.fromCString(segment.asSlice(
          NativeLayouts.MEMBER_PEER_CHANNEL_NAME_OFFSET, 256), 256);
        String endpoint = NativeHelpers.fromCString(segment.asSlice(
          NativeLayouts.MEMBER_PEER_ENDPOINT_OFFSET, 256), 256);
        int routingSize = segment.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.MEMBER_PEER_ROUTING_ID_OFFSET
            + NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        byte[] routingBytes = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment,
              NativeLayouts.MEMBER_PEER_ROUTING_ID_OFFSET
                + NativeLayouts.ROUTING_ID_DATA_OFFSET,
              MemorySegment.ofArray(routingBytes), 0, routingSize);
        }
        int weight = segment.get(ValueLayout.JAVA_INT,
          NativeLayouts.MEMBER_PEER_WEIGHT_OFFSET);
        long value = segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
          NativeLayouts.MEMBER_PEER_VALUE_OFFSET);
        return new MemberPeerEntry(autoConnectType, serviceRole, channelName,
          endpoint, InternalAccess.routingIdFromTrusted(routingBytes), value,
          weight);
    }

}
