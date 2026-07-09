/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import systems.zlink.contracts.messaging.Message;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.util.ArrayList;
import java.util.List;

final class NativeRouterReceive {
    private static final MethodHandle MH_JAVA_ROUTER_RECV =
        NativeSymbols.optionalDowncall(
            "zlink_java_router_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.JAVA_INT));

    private NativeRouterReceive() {
    }

    static int recv(MemorySegment router,
                    MemorySegment sourceNodeRidOut,
                    MemorySegment sourceSpotRidOut,
                    MemorySegment requestSeqOut,
                    MemorySegment partsOut,
                    MemorySegment partCountOut,
                    int flags,
                    NativeMultipartScratch scratch) {
        try {
            if (MH_JAVA_ROUTER_RECV != null) {
                return (int) MH_JAVA_ROUTER_RECV.invokeExact(router,
                    sourceNodeRidOut, sourceSpotRidOut, requestSeqOut,
                    partsOut, partCountOut, flags);
            }
            return recvWithPartLoop(router, sourceNodeRidOut, sourceSpotRidOut,
                requestSeqOut, partsOut, partCountOut, flags, scratch);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_recv_part failed", t);
        }
    }

    private static int recvWithPartLoop(MemorySegment router,
                                        MemorySegment sourceNodeRidOut,
                                        MemorySegment sourceSpotRidOut,
                                        MemorySegment requestSeqOut,
                                        MemorySegment partsOut,
                                        MemorySegment partCountOut,
                                        int flags,
                                        NativeMultipartScratch scratch) {
        scratch.reset();
        MemorySegment nodeRidPtrOut = scratch.nodeRidPtrOut;
        MemorySegment spotRidPtrOut = scratch.spotRidPtrOut;
        MemorySegment seqOut = scratch.seqOut;
        MemorySegment hasMoreOut = scratch.hasMoreOut;
        return NativeErrno.retryWhileInterrupted(
            () -> recvWithPartLoopOnce(router, sourceNodeRidOut,
                sourceSpotRidOut, requestSeqOut, partsOut, partCountOut, flags,
                scratch, nodeRidPtrOut, spotRidPtrOut, seqOut, hasMoreOut),
            result -> result != 0);
    }

    private static int recvWithPartLoopOnce(
        MemorySegment router,
        MemorySegment sourceNodeRidOut,
        MemorySegment sourceSpotRidOut,
        MemorySegment requestSeqOut,
        MemorySegment partsOut,
        MemorySegment partCountOut,
        int flags,
        NativeMultipartScratch scratch,
        MemorySegment nodeRidPtrOut,
        MemorySegment spotRidPtrOut,
        MemorySegment seqOut,
        MemorySegment hasMoreOut) {
        List<Message> receivedParts = null;
        while (true) {
            Message part = new Message();
            boolean success = false;
            try {
                int rc = Native.routerRecvPart(router, nodeRidPtrOut,
                    spotRidPtrOut, seqOut,
                    InternalAccess.messageNativeHandle(part),
                    hasMoreOut, flags);
                if (rc != 0) {
                    if (receivedParts != null) {
                        Message.closeAll(receivedParts);
                    }
                    return rc;
                }
                success = true;
                if (receivedParts == null) {
                    sourceNodeRidOut.set(ValueLayout.ADDRESS, 0,
                        nodeRidPtrOut.get(ValueLayout.ADDRESS, 0));
                    sourceSpotRidOut.set(ValueLayout.ADDRESS, 0,
                        spotRidPtrOut.get(ValueLayout.ADDRESS, 0));
                    requestSeqOut.set(ValueLayout.JAVA_LONG, 0,
                        seqOut.get(ValueLayout.JAVA_LONG, 0));
                }
                InternalAccess.messageFinishReceive(part,
                    hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0);
                boolean partHasMore = InternalAccess.messageMore(part);
                if (!partHasMore && receivedParts == null) {
                    MemorySegment parts = scratch.allocateParts(1);
                    InternalAccess.messageMoveTo(part,
                        Native.nthPart(parts, 0));
                    part.close();
                    partsOut.set(ValueLayout.ADDRESS, 0, parts);
                    partCountOut.set(ValueLayout.JAVA_LONG, 0,
                        scratch.partCount());
                    return 0;
                }
                if (receivedParts == null) {
                    receivedParts = new ArrayList<>();
                }
                receivedParts.add(part);
                if (!partHasMore) {
                    MemorySegment parts =
                        scratch.materializeParts(receivedParts);
                    partsOut.set(ValueLayout.ADDRESS, 0, parts);
                    partCountOut.set(ValueLayout.JAVA_LONG, 0,
                        scratch.partCount());
                    return 0;
                }
            } finally {
                if (!success) {
                    try {
                        part.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
        }
    }
}
