/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.spot.ActorPart;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import systems.zlink.internal.ReceivedPartCursor;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.List;
import java.util.function.BiConsumer;

/**
 * Direct bridge for package-private binding internals used by service
 * subpackages. This is not public contract and must not be used by
 * applications.
 *
 * @hidden
 */
@Deprecated(forRemoval = true)
final class InternalAccess {
    private static final MethodHandle DISCOVERY_HANDLE =
        handleMethod(Discovery.class);
    private static final MethodHandle SPOT_HANDLE = handleMethod(Spot.class);
    private static final MethodHandle SPOT_NODE_HANDLE =
        handleMethod(SpotNode.class);

    private InternalAccess() {
    }

    public static MemorySegment contextHandle(Context context) {
        return context.handle();
    }

    public static MemorySegment discoveryHandle(Discovery discovery) {
        try {
            return (MemorySegment) DISCOVERY_HANDLE.invokeExact(discovery);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment socketHandle(Socket socket) {
        return socket.handle();
    }

    public static MemorySegment spotHandle(Spot spot) {
        try {
            return (MemorySegment) SPOT_HANDLE.invokeExact(spot);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment spotNodeHandle(SpotNode node) {
        try {
            return (MemorySegment) SPOT_NODE_HANDLE.invokeExact(node);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment spotDispatchSubject(SpotDispatchInfo info) {
        return info.subject();
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject) {
        return new SpotDispatchInfo(event, subjectKind, subject);
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject,
      List<ActorPart> actorParts) {
        return new SpotDispatchInfo(event, subjectKind, subject, actorParts);
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject,
      Timer timer,
      String channelName,
      List<ActorPart> actorParts) {
        return new SpotDispatchInfo(event, subjectKind, subject, timer,
          channelName, actorParts);
    }

    public static Timer timerFromBorrowedHandle(MemorySegment handle) {
        return Timer.fromBorrowedHandle(handle);
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
                                        ReceivedPartCursor cursor,
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
                                        ReceivedPartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        return new Received(routingId, spotRid, firstPart, cursor, requestSeq,
            hasRequestSeq, replySender, onTerminalState);
    }

    public static TopicMessage topicMessage(RoutingId routingId,
                                            String serviceName,
                                            String topicId,
                                            Message[] parts) {
        return new TopicMessage(routingId, serviceName, topicId, parts);
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

    public static ZlinkException zlinkExceptionFromLastError(String operation) {
        return ZlinkException.fromLastError(operation);
    }

    public static ZlinkException zlinkExceptionFromErrno(String operation,
                                                         int errno) {
        return ZlinkException.fromErrno(operation, errno);
    }

    private static MethodHandle handleMethod(Class<?> type) {
        try {
            return MethodHandles.privateLookupIn(type, MethodHandles.lookup())
              .findVirtual(type, "handleInternal",
                MethodType.methodType(MemorySegment.class));
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static RuntimeException unchecked(Throwable t) {
        if (t instanceof RuntimeException runtime) {
            return runtime;
        }
        if (t instanceof Error error) {
            throw error;
        }
        throw new AssertionError(t);
    }
}
