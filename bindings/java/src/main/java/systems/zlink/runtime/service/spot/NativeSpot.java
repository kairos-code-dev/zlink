/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotStatus;
import systems.zlink.contracts.service.spot.SubscriptionKind;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeServiceSymbols;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;

public final class NativeSpot implements Spot {
    private static final int PUBLISH_OPT_NODROP = 0x3630;

    private final NativeMeshNode owner;
    private final boolean owned;
    private MemorySegment handle;

    static {
        InternalAccess.register((InternalAccess.SpotAccess)
            spot -> ((NativeSpot) spot).handle);
    }

    NativeSpot(NativeMeshNode owner, MemorySegment handle, boolean owned) {
        this.owner = owner;
        this.handle = handle;
        this.owned = owned;
    }

    MemorySegment handle() {
        return handle;
    }

    void destroyOwned() {
        if (owned && handle != null && handle.address() != 0) {
            NativeServiceSymbols.spotDestroy(handle);
            handle = MemorySegment.NULL;
        }
    }

    @Override
    public SpotStatus status() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = ServiceInterop.allocStamped(arena, ServiceLayouts.SPOT_STATUS);
            MeshCalls.configOk(NativeServiceSymbols.spotStatus(handle, out));
            return ServiceInterop.spotStatusFromNative(out);
        }
    }

    @Override
    public RoutingId routingId() {
        return status().spotRid();
    }

    @Override
    public void sendToChannel(String channel, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(channel, "channel");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment name = NativeHelpers.toCString(arena, channel);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.spotSendToChannel(handle, name, MemorySegment.NULL,
                array, n, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_spot_send_to_channel");
        }
    }

    @Override
    public OperationId requestToChannel(String channel, List<Message> parts, SendFlags flags,
                                        Duration timeout) {
        Objects.requireNonNull(channel, "channel");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment name = NativeHelpers.toCString(arena, channel);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.spotRequestToChannel(handle, name, MemorySegment.NULL,
                array, n, opid, flags.value(), MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, array, n, "zlink_spot_request_to_channel");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public void sendToSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
                           long targetSpotGeneration, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(targetSpotRid, "targetSpotRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = ServiceInterop.routingIdToNative(arena, targetNodeRid);
            MemorySegment spotRid = ServiceInterop.routingIdToNative(arena, targetSpotRid);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.spotSendToSpot(handle, nodeRid, spotRid,
                targetSpotGeneration, MemorySegment.NULL, array, n, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_spot_send_to_spot");
        }
    }

    @Override
    public OperationId requestToSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
                                     long targetSpotGeneration, List<Message> parts,
                                     SendFlags flags, Duration timeout) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(targetSpotRid, "targetSpotRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = ServiceInterop.routingIdToNative(arena, targetNodeRid);
            MemorySegment spotRid = ServiceInterop.routingIdToNative(arena, targetSpotRid);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.spotRequestToSpot(handle, nodeRid, spotRid,
                targetSpotGeneration, MemorySegment.NULL, array, n, opid, flags.value(),
                MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, array, n, "zlink_spot_request_to_spot");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public void publish(String channel, String topic, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(channel, "channel");
        Objects.requireNonNull(topic, "topic");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment name = NativeHelpers.toCString(arena, channel);
            MemorySegment topicSeg = NativeHelpers.toCString(arena, topic);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.spotPublish(handle, name, topicSeg, MemorySegment.NULL,
                array, n, MemorySegment.NULL, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_spot_publish");
        }
    }

    @Override
    public void setNoDrop(boolean nodrop) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment value = arena.allocate(ValueLayout.JAVA_INT);
            value.set(ValueLayout.JAVA_INT, 0, nodrop ? 1 : 0);
            MeshCalls.configOk(NativeServiceSymbols.spotSetPublishOption(handle,
                PUBLISH_OPT_NODROP, value, 4));
        }
    }

    @Override
    public void setSubscription(String channel, String topicFilter, SubscriptionKind kind) {
        Objects.requireNonNull(channel, "channel");
        Objects.requireNonNull(topicFilter, "topicFilter");
        Objects.requireNonNull(kind, "kind");
        try (Arena arena = Arena.ofConfined()) {
            MeshCalls.configOk(NativeServiceSymbols.spotSetSubscription(handle,
                NativeHelpers.toCString(arena, channel),
                NativeHelpers.toCString(arena, topicFilter), kind.value()));
        }
    }

    @Override
    public void unsetSubscription(String channel, String topicFilter, SubscriptionKind kind) {
        Objects.requireNonNull(channel, "channel");
        Objects.requireNonNull(topicFilter, "topicFilter");
        Objects.requireNonNull(kind, "kind");
        try (Arena arena = Arena.ofConfined()) {
            MeshCalls.configOk(NativeServiceSymbols.spotUnsetSubscription(handle,
                NativeHelpers.toCString(arena, channel),
                NativeHelpers.toCString(arena, topicFilter), kind.value()));
        }
    }

    @Override
    public void close() {
        if (owned) {
            owner.releaseSpot(this);
            destroyOwned();
        }
    }
}
