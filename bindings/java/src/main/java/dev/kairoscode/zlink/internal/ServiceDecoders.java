/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.service.registry.MemberPeerEntry;
import dev.kairoscode.zlink.service.registry.ServiceEvent;
import dev.kairoscode.zlink.service.registry.ServiceEventSubjectKind;
import dev.kairoscode.zlink.service.registry.ServiceEventType;
import dev.kairoscode.zlink.service.registry.ServiceKind;
import dev.kairoscode.zlink.service.registry.ServiceRole;
import dev.kairoscode.zlink.service.registry.ServiceType;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Optional;

/**
 * Internal FFM decoders for service-layer value records.
 */
public final class ServiceDecoders {
    private ServiceDecoders() {
    }

    public static MemberPeerEntry memberPeerEntry(MemorySegment segment) {
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
        int weight = segment.get(ValueLayout.JAVA_INT,
          NativeLayouts.MEMBER_PEER_WEIGHT_OFFSET);
        long value = segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
          NativeLayouts.MEMBER_PEER_VALUE_OFFSET);
        return new MemberPeerEntry(serviceType, serviceRole, serviceName,
          endpoint, InternalAccess.routingIdFromTrusted(routingBytes), value,
          weight);
    }

    public static ServiceEvent serviceEvent(MemorySegment event) {
        int kindValue = event.get(ValueLayout.JAVA_INT,
          NativeLayouts.SERVICE_EVENT_SERVICE_KIND_OFFSET);
        int routingSize = event.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.SERVICE_EVENT_ROUTING_ID_OFFSET
            + NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(event,
              NativeLayouts.SERVICE_EVENT_ROUTING_ID_OFFSET
                + NativeLayouts.ROUTING_ID_DATA_OFFSET,
              MemorySegment.ofArray(routing), 0, routingSize);
        }
        return new ServiceEvent(
          ServiceKind.fromValue(kindValue),
          ServiceEventType.fromValue(Integer.toUnsignedLong(event.get(
            ValueLayout.JAVA_INT, NativeLayouts.SERVICE_EVENT_EVENT_TYPE_OFFSET))),
          event.get(ValueLayout.JAVA_INT, NativeLayouts.SERVICE_EVENT_STATUS_OFFSET),
          event.get(ValueLayout.JAVA_INT,
            NativeLayouts.SERVICE_EVENT_ERROR_CODE_OFFSET),
          Integer.toUnsignedLong(event.get(ValueLayout.JAVA_INT,
            NativeLayouts.SERVICE_EVENT_VALUE_OFFSET)),
          event.get(ValueLayout.JAVA_INT,
            NativeLayouts.SERVICE_EVENT_DETAIL_FLAGS_OFFSET),
          NativeHelpers.fromCString(event.asSlice(
            NativeLayouts.SERVICE_EVENT_SERVICE_NAME_OFFSET, 256), 256),
          NativeHelpers.fromCString(event.asSlice(
            NativeLayouts.SERVICE_EVENT_ENDPOINT_OFFSET, 256), 256),
          routingSize == 0 ? Optional.empty()
            : Optional.of(InternalAccess.routingIdFromTrusted(routing)),
          NativeHelpers.fromCString(event.asSlice(
            NativeLayouts.SERVICE_EVENT_SUBJECT_OFFSET, 256), 256),
          ServiceEventSubjectKind.fromValue(event.get(ValueLayout.JAVA_INT,
            NativeLayouts.SERVICE_EVENT_SUBJECT_KIND_OFFSET)));
    }
}
