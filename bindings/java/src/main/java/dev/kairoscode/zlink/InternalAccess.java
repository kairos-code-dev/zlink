/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.spot.Spot;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.function.BiConsumer;

/**
 * Direct bridge for package-private binding internals used by service
 * subpackages.
 */
public final class InternalAccess {
    private InternalAccess() {
    }

    public static MemorySegment contextHandle(Context context) {
        return context.handle();
    }

    public static MemorySegment discoveryHandle(Discovery discovery) {
        return discovery.handleInternal();
    }

    public static MemorySegment socketHandle(Socket socket) {
        return socket.handle();
    }

    public static MemorySegment spotHandle(Spot spot) {
        return spot.handleInternal();
    }

    public static MemorySegment messageDataSegment(Message message) {
        return message.dataSegment();
    }

    public static MemorySegment messageDataSegment(Message message,
                                                   int knownSize) {
        return message.dataSegment(knownSize);
    }

    public static void messageCopyTo(Message message, MemorySegment destination) {
        message.copyTo(destination);
    }

    public static void messageMoveTo(Message message, MemorySegment destination) {
        message.moveTo(destination);
    }

    public static MemorySegment messageNativeHandle(Message message) {
        return message.nativeHandle();
    }

    public static void messageSetMore(Message message, boolean more) {
        message.setMore(more);
    }

    public static boolean messageMore(Message message) {
        return message.more();
    }

    public static void messageFinishReceive(Message message, boolean more) {
        message.finishReceive(more);
    }

    public static Object messageTransferTo(Message message,
                                           MemorySegment destination) {
        return message.transferTo(destination);
    }

    public static void messageRestoreFromNative(Message message,
                                                MemorySegment source,
                                                boolean moreFlag,
                                                Object anchor) {
        message.restoreFromNative(source, moreFlag, anchor);
    }

    public static Message messageSharedCopyOf(Message message) {
        return Message.sharedCopyOf(message);
    }

    public static Message[] messageFromMsgVector(MemorySegment partsAddr,
                                                 long count) {
        return Message.fromMsgVector(partsAddr, count);
    }

    public static Message[] messageFromOwnedMsgVector(MemorySegment partsAddr,
                                                      long count) {
        return Message.fromOwnedMsgVector(partsAddr, count);
    }

    public static Message[] messageFromOwnedMsgVectorShared(
      MemorySegment partsAddr,
      long count) {
        return Message.fromOwnedMsgVectorShared(partsAddr, count);
    }

    public static Received received(RoutingId routingId, Message[] parts) {
        return received(routingId, null, parts, 0L, false, null);
    }

    public static Received received(RoutingId routingId,
                                    RoutingId spotRid,
                                    Message[] parts,
                                    long requestSeq,
                                    boolean hasRequestSeq,
                                    BiConsumer<List<Message>, SendFlags> replySender) {
        return new Received(routingId, spotRid, parts, true, requestSeq,
            hasRequestSeq, replySender);
    }

    public static Received receivedLazy(byte[] routingIdBytes,
                                        byte[] spotRidBytes,
                                        Message firstPart,
                                        Received.PartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        return new Received(routingIdBytes, spotRidBytes, firstPart, cursor,
            requestSeq, hasRequestSeq, replySender, onTerminalState);
    }

    public static Received receivedLazy(RoutingId routingId,
                                        RoutingId spotRid,
                                        Message firstPart,
                                        Received.PartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        return new Received(routingId, spotRid, firstPart, cursor, requestSeq,
            hasRequestSeq, replySender, onTerminalState);
    }

    public static void receivedForceMaterialize(Received received) {
        received.forceMaterialize();
    }

    public static List<Message> receivedTakeParts(Received received) {
        return received.takeParts();
    }

    public static boolean inCallback() {
        return SocketCore.inCallback();
    }

    public static void enterCallback() {
        SocketCore.enterCallback();
    }

    public static void leaveCallback() {
        SocketCore.leaveCallback();
    }

    public static RoutingId routingIdFromTrusted(byte[] value) {
        return RoutingId.fromTrusted(value);
    }

    public static byte[] routingIdTrustedBytes(RoutingId routingId) {
        return routingId.trustedBytes();
    }
}
