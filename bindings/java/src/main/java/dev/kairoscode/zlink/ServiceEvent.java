/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record ServiceEvent(ServiceKind serviceKind,
                           ServiceEventType eventType,
                           int status,
                           int errorCode,
                           long value,
                           long detailFlags,
                           String serviceName,
                           String endpoint,
                           RoutingId routingId,
                           String subject,
                           ServiceEventSubjectKind subjectKind) {
    static ServiceEvent fromNative(MemorySegment event) {
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
          Integer.toUnsignedLong(event.get(ValueLayout.JAVA_INT,
            NativeLayouts.SERVICE_EVENT_DETAIL_FLAGS_OFFSET)),
          NativeHelpers.fromCString(event.asSlice(
            NativeLayouts.SERVICE_EVENT_SERVICE_NAME_OFFSET, 256), 256),
          NativeHelpers.fromCString(event.asSlice(
            NativeLayouts.SERVICE_EVENT_ENDPOINT_OFFSET, 256), 256),
          routingSize == 0 ? null : RoutingId.copyOf(routing),
          NativeHelpers.fromCString(event.asSlice(
            NativeLayouts.SERVICE_EVENT_SUBJECT_OFFSET, 256), 256),
          ServiceEventSubjectKind.fromValue(event.get(ValueLayout.JAVA_INT,
            NativeLayouts.SERVICE_EVENT_SUBJECT_KIND_OFFSET)));
    }
}
