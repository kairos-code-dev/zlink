/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.RoutingId;
import java.lang.foreign.MemorySegment;
import java.util.Optional;

public record ServiceEvent(ServiceKind serviceKind,
                           ServiceEventType eventType,
                           int status,
                           int errorCode,
                           long value,
                           int detailFlags,
                           String serviceName,
                           String endpoint,
                           Optional<RoutingId> routingId,
                           String subject,
                           ServiceEventSubjectKind subjectKind) {
    static ServiceEvent fromNative(MemorySegment event) {
        return dev.kairoscode.zlink.internal.ServiceDecoders.serviceEvent(
          event);
    }
}
