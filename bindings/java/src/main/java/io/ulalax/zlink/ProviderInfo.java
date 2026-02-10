/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.NativeHelpers;
import io.ulalax.zlink.internal.NativeLayouts;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record ProviderInfo(String serviceName, String endpoint, byte[] routingId,
                           int weight, long registeredAt) {
    static ProviderInfo from(MemorySegment segment) {
        MemorySegment name = segment.asSlice(NativeLayouts.PROVIDER_SERVICE_OFFSET, 256);
        MemorySegment endpointSeg = segment.asSlice(NativeLayouts.PROVIDER_ENDPOINT_OFFSET, 256);
        String service = NativeHelpers.fromCString(name, 256);
        String endpoint = NativeHelpers.fromCString(endpointSeg, 256);
        int routingSize = segment.get(ValueLayout.JAVA_BYTE, NativeLayouts.PROVIDER_ROUTING_OFFSET) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment, NativeLayouts.PROVIDER_ROUTING_OFFSET + 1,
                MemorySegment.ofArray(routing), 0, routingSize);
        }
        int weight = segment.get(ValueLayout.JAVA_INT, NativeLayouts.PROVIDER_WEIGHT_OFFSET);
        long registered = segment.get(ValueLayout.JAVA_LONG, NativeLayouts.PROVIDER_REGISTERED_OFFSET);
        return new ProviderInfo(service, endpoint, routing, weight, registered);
    }
}
