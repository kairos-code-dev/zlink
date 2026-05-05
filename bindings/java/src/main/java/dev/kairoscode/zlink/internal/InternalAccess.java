/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendFlags;
import java.lang.foreign.MemorySegment;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.util.List;
import java.util.function.BiConsumer;

/**
 * Compatibility wrapper that forwards to the direct root-package bridge.
 */
public final class InternalAccess {
    private InternalAccess() {}

    public static MemorySegment contextHandle(Context context) {
        return dev.kairoscode.zlink.InternalAccess.contextHandle(context);
    }

    public static MemorySegment discoveryHandle(Discovery discovery) {
        return dev.kairoscode.zlink.InternalAccess.discoveryHandle(discovery);
    }

    public static MemorySegment socketHandle(Socket socket) {
        return dev.kairoscode.zlink.InternalAccess.socketHandle(socket);
    }

    public static MemorySegment spotHandle(Spot spot) {
        return dev.kairoscode.zlink.InternalAccess.spotHandle(spot);
    }

    public static MemorySegment spotNodeHandle(SpotNode node) {
        return dev.kairoscode.zlink.InternalAccess.spotNodeHandle(node);
    }

    public static MemorySegment messageDataSegment(Message message) {
        return dev.kairoscode.zlink.InternalAccess.messageDataSegment(message);
    }

    public static MemorySegment messageDataSegment(Message message,
                                                   int knownSize) {
        return dev.kairoscode.zlink.InternalAccess.messageDataSegment(message,
            knownSize);
    }

    public static void messageCopyTo(Message message, MemorySegment destination) {
        dev.kairoscode.zlink.InternalAccess.messageCopyTo(message, destination);
    }

    public static void messageMoveTo(Message message, MemorySegment destination) {
        dev.kairoscode.zlink.InternalAccess.messageMoveTo(message, destination);
    }

    public static MemorySegment messageNativeHandle(Message message) {
        return dev.kairoscode.zlink.InternalAccess.messageNativeHandle(message);
    }

    public static void messageSetMore(Message message, boolean more) {
        dev.kairoscode.zlink.InternalAccess.messageSetMore(message, more);
    }

    public static boolean messageMore(Message message) {
        return dev.kairoscode.zlink.InternalAccess.messageMore(message);
    }

    public static void messageFinishReceive(Message message, boolean more) {
        dev.kairoscode.zlink.InternalAccess.messageFinishReceive(message, more);
    }

    public static Object messageTransferTo(Message message,
                                           MemorySegment destination) {
        return dev.kairoscode.zlink.InternalAccess.messageTransferTo(message,
            destination);
    }

    public static void messageRestoreFromNative(Message message,
                                                MemorySegment source,
                                                boolean moreFlag,
                                                Object anchor) {
        dev.kairoscode.zlink.InternalAccess.messageRestoreFromNative(message,
            source, moreFlag, anchor);
    }

    public static Message messageSharedCopyOf(Message message) {
        return dev.kairoscode.zlink.InternalAccess.messageSharedCopyOf(message);
    }

    public static Message[] messageFromMsgVector(MemorySegment partsAddr,
                                                 long count) {
        return dev.kairoscode.zlink.InternalAccess.messageFromMsgVector(
            partsAddr, count);
    }

    public static Message[] messageFromOwnedMsgVector(MemorySegment partsAddr,
                                                      long count) {
        return dev.kairoscode.zlink.InternalAccess.messageFromOwnedMsgVector(
            partsAddr, count);
    }

    public static Message[] messageFromOwnedMsgVectorShared(
                                                  MemorySegment partsAddr,
                                                  long count) {
        return dev.kairoscode.zlink.InternalAccess
            .messageFromOwnedMsgVectorShared(partsAddr, count);
    }

    public static Received received(dev.kairoscode.zlink.RoutingId routingId,
                                    Message[] parts) {
        return dev.kairoscode.zlink.InternalAccess.received(routingId, parts);
    }

    public static Received received(dev.kairoscode.zlink.RoutingId routingId,
                                    dev.kairoscode.zlink.RoutingId spotRid,
                                    Message[] parts,
                                    long requestSeq,
                                    boolean hasRequestSeq,
                                    BiConsumer<List<Message>, SendFlags> replySender) {
        return dev.kairoscode.zlink.InternalAccess.received(routingId, spotRid,
            parts, requestSeq, hasRequestSeq, replySender);
    }

    public static Received receivedLazy(byte[] routingIdBytes,
                                        byte[] spotRidBytes,
                                        Message firstPart,
                                        Received.PartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        return dev.kairoscode.zlink.InternalAccess.receivedLazy(
            routingIdBytes, spotRidBytes, firstPart, cursor, requestSeq,
            hasRequestSeq, replySender, onTerminalState);
    }

    public static Received receivedLazy(RoutingId routingId,
                                        RoutingId spotRid,
                                        Message firstPart,
                                        Received.PartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        return dev.kairoscode.zlink.InternalAccess.receivedLazy(routingId,
            spotRid, firstPart, cursor, requestSeq, hasRequestSeq, replySender,
            onTerminalState);
    }

    public static void receivedForceMaterialize(Received received) {
        dev.kairoscode.zlink.InternalAccess.receivedForceMaterialize(received);
    }

    @SuppressWarnings("unchecked")
    public static List<Message> receivedTakeParts(Received received) {
        return dev.kairoscode.zlink.InternalAccess.receivedTakeParts(received);
    }

    public static boolean inCallback() {
        return dev.kairoscode.zlink.InternalAccess.inCallback();
    }

    public static void enterCallback() {
        dev.kairoscode.zlink.InternalAccess.enterCallback();
    }

    public static void leaveCallback() {
        dev.kairoscode.zlink.InternalAccess.leaveCallback();
    }

    public static RoutingId routingIdFromTrusted(byte[] value) {
        return dev.kairoscode.zlink.InternalAccess.routingIdFromTrusted(value);
    }

    public static byte[] routingIdTrustedBytes(RoutingId routingId) {
        return dev.kairoscode.zlink.InternalAccess.routingIdTrustedBytes(
            routingId);
    }
}
