/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record MemberPeerEntry(ServiceType serviceType, ServiceRole serviceRole,
                              String serviceName, String endpoint,
                              RoutingId routingId, long value) {
    public static MemberPeerEntry fromNative(MemorySegment segment) {
        ServiceType serviceType = ServiceType.fromValue(
          segment.get(ValueLayout.JAVA_SHORT,
            NativeLayouts.MEMBER_PEER_SERVICE_TYPE_OFFSET)
            & 0xFFFF);
        ServiceRole serviceRole = ServiceRole.fromValue(
          segment.get(ValueLayout.JAVA_SHORT,
            NativeLayouts.MEMBER_PEER_SERVICE_ROLE_OFFSET)
            & 0xFFFF);
        String serviceName = NativeHelpers.fromCString(segment.asSlice(
          NativeLayouts.MEMBER_PEER_SERVICE_NAME_OFFSET, 256), 256);
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
        long value = segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
          NativeLayouts.MEMBER_PEER_VALUE_OFFSET);
        return new MemberPeerEntry(serviceType, serviceRole, serviceName,
          endpoint, RoutingId.fromBytes(routingBytes), value);
    }
}
