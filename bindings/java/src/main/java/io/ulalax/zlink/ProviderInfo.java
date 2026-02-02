package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import io.ulalax.zlink.internal.NativeHelpers;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record ProviderInfo(String serviceName, String endpoint, byte[] routingId,
                           int weight, long registeredAt) {
    static ProviderInfo from(MemorySegment segment) {
        MemorySegment name = segment.asSlice(Native.PROVIDER_SERVICE_OFFSET, 256);
        MemorySegment endpointSeg = segment.asSlice(Native.PROVIDER_ENDPOINT_OFFSET, 256);
        String service = NativeHelpers.fromCString(name, 256);
        String endpoint = NativeHelpers.fromCString(endpointSeg, 256);
        byte[] routing = new byte[255];
        MemorySegment.copy(segment, Native.PROVIDER_ROUTING_OFFSET, MemorySegment.ofArray(routing), 0, 255);
        int weight = segment.get(ValueLayout.JAVA_INT, Native.PROVIDER_WEIGHT_OFFSET);
        long registered = segment.get(ValueLayout.JAVA_LONG, Native.PROVIDER_REGISTERED_OFFSET);
        return new ProviderInfo(service, endpoint, routing, weight, registered);
    }
}
