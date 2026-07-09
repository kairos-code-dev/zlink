/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RequestCallback;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.service.spot.RequestOperation;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotRouteBridge;
import systems.zlink.contracts.service.spot.SpotRouteBridgeEndpointOptions;
import systems.zlink.runtime.messaging.MessageOperations;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.RoutedRequestSupport;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import systems.zlink.runtime.nativeapi.RequestReplySupport;

final class NativeSpotRouteBridge implements SpotRouteBridge {
    private MemorySegment handle;
    private final Map<String, MemorySegment> endpointHandles =
        new ConcurrentHashMap<>();

    NativeSpotRouteBridge(Context ctx, SpotNode node) {
        Objects.requireNonNull(ctx, "ctx");
        Objects.requireNonNull(node, "node");
        handle = Native.spotRouteBridgeNew(InternalAccess.contextHandle(ctx),
            InternalAccess.spotNodeHandle(node), MemorySegment.NULL);
        if (handle == null || handle.address() == 0) {
            throw InternalAccess.zlinkExceptionFromLastError(
                systems.zlink.contracts.errors.ErrorCategory.CONFIG);
        }
    }

    @Override
    public void attachRouterChannel(String channelName, RouterSocket router) {
        attachRouterChannel(channelName, router, null);
    }

    @Override
    public void attachRouterChannel(
        String channelName,
        RouterSocket router,
        SpotRouteBridgeEndpointOptions options) {
        Objects.requireNonNull(router, "router");
        String normalized = requireChannelName(channelName);
        MemorySegment routerHandle = InternalAccess.socketHandle(router);
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotRouteBridgeAttachRouterChannel(handle(),
                NativeHelpers.toCString(arena, normalized), routerHandle,
                nativeEndpointOptions(arena, options));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                    systems.zlink.contracts.errors.ErrorCategory.CONFIG);
            }
        }
        endpointHandles.put(normalized, routerHandle);
    }

    @Override
    public SendOperation send(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid) {
        String normalized = requireChannelName(channelName);
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(targetSpotRid, "targetSpotRid");
        return MessageOperations.send((parts, flags) ->
            sendInternal(normalized, targetNodeRid, targetSpotRid, parts, flags));
    }

    @Override
    public RequestOperation request(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid) {
        String normalized = requireChannelName(channelName);
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(targetSpotRid, "targetSpotRid");
        return MessageOperations.request(
            (parts, flags, timeout) ->
                requestAsync(normalized, targetNodeRid, targetSpotRid, parts, flags, timeout),
            (parts, callback, flags, timeout) ->
                requestCallback(normalized, targetNodeRid, targetSpotRid, parts, callback,
                    flags, timeout));
    }

    @Override
    public boolean handleRouterReceived(
        String channelName,
        RoutingId sourceNodeRid,
        long requestSeq,
        List<Message> parts) {
        String normalized = requireChannelName(channelName);
        Objects.requireNonNull(sourceNodeRid, "sourceNodeRid");
        Objects.requireNonNull(parts, "parts");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeParts = copyParts(arena, parts);
            MemorySegment handledOut = arena.allocate(ValueLayout.JAVA_BYTE);
            int rc = Native.spotRouteBridgeHandleRouterReceived(
                handle(), NativeHelpers.toCString(arena, normalized),
                ActorInterop.nativeRoutingId(arena, sourceNodeRid), requestSeq,
                nativeParts, parts.size(), handledOut);
            if (rc != 0) {
                MessagePartsBuffer.closeNativeArray(nativeParts, parts.size());
                throw InternalAccess.zlinkExceptionFromLastError(
                    systems.zlink.contracts.errors.ErrorCategory.RECV);
            }
            boolean handled = handledOut.get(ValueLayout.JAVA_BYTE, 0) != 0;
            if (!handled) {
                MessagePartsBuffer.closeNativeArray(nativeParts, parts.size());
            }
            return handled;
        }
    }

    @Override
    public int drain() {
        int rc = Native.spotRouteBridgeDrain(handle());
        if (rc < 0) {
            throw InternalAccess.zlinkExceptionFromLastError(
                systems.zlink.contracts.errors.ErrorCategory.RECV);
        }
        return rc;
    }

    @Override
    public void close() {
        MemorySegment current = handle;
        if (current == null || current.address() == 0) {
            return;
        }
        handle = MemorySegment.NULL;
        RequestProgressPump.stopSpotRouteBridgeProgress(current);
        endpointHandles.clear();
        int rc = Native.spotRouteBridgeClose(current);
        if (rc != 0) {
            throw InternalAccess.zlinkExceptionFromLastError(
                systems.zlink.contracts.errors.ErrorCategory.CLOSE);
        }
    }

    private boolean sendInternal(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeParts =
                RoutedRequestSupport.movePayloadToNative(arena, parts);
            int rc = Native.spotRouteBridgeSend(handle(),
                NativeHelpers.toCString(arena, channelName),
                ActorInterop.nativeRoutingId(arena, targetNodeRid),
                ActorInterop.nativeRoutingId(arena, targetSpotRid),
                nativeParts, parts.size(), flags.value());
            if (rc != 0) {
                NativeMessage.multipartClose(nativeParts, parts.size());
                throw InternalAccess.zlinkExceptionFromLastError(
                    systems.zlink.contracts.errors.ErrorCategory.SUBMIT);
            }
            return true;
        }
    }

    private CompletableFuture<List<Message>> requestAsync(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        SendFlags flags,
        Duration timeout) {
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<systems.zlink.contracts.messaging.Received> future =
            RoutedRequestSupport.registerPending(requestId, timeoutMs);
        RequestProgressPump.trackSocketRequest(future,
            endpointHandle(channelName), "zlink-spot-route-bridge-request");
        RequestProgressPump.trackSpotRouteBridgeRequest(future,
            handle(), "zlink-spot-route-bridge-drain");
        try {
            submitRequest(channelName, targetNodeRid, targetSpotRid, parts, flags,
                RequestReplySupport.toTimeoutInt(timeoutMs), requestId);
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(InternalAccess::receivedTakeParts);
    }

    private boolean requestCallback(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Void> progress = RoutedRequestSupport.registerDirectPending(
            requestId, timeoutMs, callback::onComplete);
        try {
            RequestProgressPump.trackSocketRequest(progress,
                endpointHandle(channelName), "zlink-spot-route-bridge-request");
            RequestProgressPump.trackSpotRouteBridgeRequest(progress,
                handle(), "zlink-spot-route-bridge-drain");
            submitRequest(channelName, targetNodeRid, targetSpotRid, parts, flags,
                RequestReplySupport.toTimeoutInt(timeoutMs), requestId);
            return true;
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removeDirectPending(requestId);
            if (ex instanceof ZlinkSubmitException submitException
                && flags == SendFlags.DONT_WAIT
                && submitException.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    private void submitRequest(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        SendFlags flags,
        int timeoutMs,
        long requestId) {
        Objects.requireNonNull(flags, "flags");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeParts =
                RoutedRequestSupport.movePayloadToNative(arena, parts);
            int rc = Native.spotRouteBridgeRequest(handle(),
                NativeHelpers.toCString(arena, channelName),
                ActorInterop.nativeRoutingId(arena, targetNodeRid),
                ActorInterop.nativeRoutingId(arena, targetSpotRid), nativeParts,
                parts.size(), RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId), flags.value(),
                timeoutMs);
            if (rc != 0) {
                NativeMessage.multipartClose(nativeParts, parts.size());
                throw InternalAccess.zlinkExceptionFromLastError(
                    systems.zlink.contracts.errors.ErrorCategory.SUBMIT);
            }
        }
    }

    private MemorySegment endpointHandle(String channelName) {
        MemorySegment endpoint = endpointHandles.get(channelName);
        if (endpoint == null || endpoint.address() == 0) {
            throw new IllegalStateException(
                "spot route bridge channel is not attached: " + channelName);
        }
        return endpoint;
    }

    private MemorySegment handle() {
        if (handle == null || handle.address() == 0) {
            throw new IllegalStateException("spot route bridge is closed");
        }
        return handle;
    }

    private static String requireChannelName(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        if (channelName.isEmpty()) {
            throw new IllegalArgumentException("channelName must not be empty");
        }
        return channelName;
    }

    private static MemorySegment nativeEndpointOptions(
        Arena arena,
        SpotRouteBridgeEndpointOptions options) {
        if (options == null) {
            return MemorySegment.NULL;
        }

        MemorySegment segment = arena.allocate(
            NativeLayouts.SPOT_ROUTE_BRIDGE_ENDPOINT_OPTIONS_LAYOUT);
        segment.set(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_ROUTE_BRIDGE_ENDPOINT_OPTIONS_STRUCT_SIZE_OFFSET,
            (int) NativeLayouts.SPOT_ROUTE_BRIDGE_ENDPOINT_OPTIONS_LAYOUT.byteSize());
        segment.set(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_ROUTE_BRIDGE_ENDPOINT_OPTIONS_CAPABILITIES_OFFSET,
            options.capabilities().value());
        segment.set(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_ROUTE_BRIDGE_ENDPOINT_OPTIONS_INBOUND_RELAY_POLICY_OFFSET,
            options.inboundRelayPolicy());
        return segment;
    }

    private static MemorySegment copyParts(Arena arena, List<Message> parts) {
        MessagePartsBuffer buffer = new MessagePartsBuffer();
        for (Message part : parts) {
            buffer.add(part);
        }
        return buffer.copyToNativeArray(arena);
    }
}
