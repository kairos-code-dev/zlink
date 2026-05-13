/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.internal;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.Received;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.Socket;
import systems.zlink.SpotDispatchEvent;
import systems.zlink.SpotDispatchInfo;
import systems.zlink.SpotDispatchSubjectKind;
import systems.zlink.Timer;
import systems.zlink.TopicMessage;
import systems.zlink.ZlinkException;
import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.spot.ActorPart;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.List;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;

/**
 * Non-exported bridge for package-private root-package internals.
 */
public final class InternalAccess {
    private static final MethodHandle CONTEXT_HANDLE =
        virtualMethod(Context.class, "handle", MemorySegment.class);
    private static final MethodHandle DISCOVERY_HANDLE =
        virtualMethod(Discovery.class, "handleInternal", MemorySegment.class);
    private static final MethodHandle SOCKET_HANDLE =
        virtualMethod(Socket.class, "handle", MemorySegment.class);
    private static final MethodHandle SPOT_HANDLE =
        virtualMethod(Spot.class, "handleInternal", MemorySegment.class);
    private static final MethodHandle SPOT_NODE_HANDLE =
        virtualMethod(SpotNode.class, "handleInternal", MemorySegment.class);
    private static final MethodHandle SPOT_DISPATCH_SUBJECT =
        virtualMethod(SpotDispatchInfo.class, "subject", MemorySegment.class);
    private static final MethodHandle SPOT_DISPATCH_INFO =
        constructor(SpotDispatchInfo.class, SpotDispatchEvent.class,
          SpotDispatchSubjectKind.class, MemorySegment.class);
    private static final MethodHandle SPOT_DISPATCH_INFO_ACTOR =
        constructor(SpotDispatchInfo.class, SpotDispatchEvent.class,
          SpotDispatchSubjectKind.class, MemorySegment.class, List.class);
    private static final MethodHandle SPOT_DISPATCH_INFO_FULL =
        constructor(SpotDispatchInfo.class, SpotDispatchEvent.class,
          SpotDispatchSubjectKind.class, MemorySegment.class, Timer.class,
          String.class, List.class);
    private static final MethodHandle TIMER_FROM_BORROWED_HANDLE =
        staticMethod(Timer.class, "fromBorrowedHandle", Timer.class,
          MemorySegment.class);
    private static final MethodHandle MESSAGE_DATA_SEGMENT =
        virtualMethod(Message.class, "dataSegment", MemorySegment.class);
    private static final MethodHandle MESSAGE_DATA_SEGMENT_KNOWN =
        virtualMethod(Message.class, "dataSegment", MemorySegment.class,
          int.class);
    private static final MethodHandle MESSAGE_COPY_TO =
        virtualMethod(Message.class, "copyTo", void.class, MemorySegment.class);
    private static final MethodHandle MESSAGE_MOVE_TO =
        virtualMethod(Message.class, "moveTo", void.class, MemorySegment.class);
    private static final MethodHandle MESSAGE_NATIVE_HANDLE =
        virtualMethod(Message.class, "nativeHandle", MemorySegment.class);
    private static final MethodHandle MESSAGE_SET_MORE =
        virtualMethod(Message.class, "setMore", void.class, boolean.class);
    private static final MethodHandle MESSAGE_MORE =
        virtualMethod(Message.class, "more", boolean.class);
    private static final MethodHandle MESSAGE_FINISH_RECEIVE =
        virtualMethod(Message.class, "finishReceive", void.class,
          boolean.class);
    private static final MethodHandle MESSAGE_TRANSFER_TO =
        virtualMethod(Message.class, "transferTo", Object.class,
          MemorySegment.class);
    private static final MethodHandle MESSAGE_RESTORE_FROM_NATIVE =
        virtualMethod(Message.class, "restoreFromNative", void.class,
          MemorySegment.class, boolean.class, Object.class);
    private static final MethodHandle MESSAGE_SHARED_COPY_OF =
        staticMethod(Message.class, "sharedCopyOf", Message.class,
          Message.class);
    private static final MethodHandle MESSAGE_FROM_MSG_VECTOR =
        staticMethod(Message.class, "fromMsgVector", Message[].class,
          MemorySegment.class, long.class);
    private static final MethodHandle MESSAGE_FROM_OWNED_MSG_VECTOR =
        staticMethod(Message.class, "fromOwnedMsgVector", Message[].class,
          MemorySegment.class, long.class);
    private static final MethodHandle MESSAGE_FROM_OWNED_MSG_VECTOR_SHARED =
        staticMethod(Message.class, "fromOwnedMsgVectorShared", Message[].class,
          MemorySegment.class, long.class);
    private static final MethodHandle RECEIVED =
        constructor(Received.class, RoutingId.class, Message[].class);
    private static final MethodHandle RECEIVED_WITH_REPLY =
        constructor(Received.class, RoutingId.class, RoutingId.class,
          Message[].class, boolean.class, long.class, boolean.class,
          BiConsumer.class);
    private static final MethodHandle RECEIVED_LAZY_BYTES =
        constructor(Received.class, byte[].class, byte[].class, Message.class,
          ReceivedPartCursor.class, long.class, boolean.class, BiConsumer.class,
          Runnable.class);
    private static final MethodHandle RECEIVED_LAZY_RIDS =
        constructor(Received.class, RoutingId.class, RoutingId.class,
          Message.class, ReceivedPartCursor.class, long.class, boolean.class,
          BiConsumer.class, Runnable.class);
    private static final MethodHandle TOPIC_MESSAGE =
        constructor(TopicMessage.class, RoutingId.class,
          String.class, Message[].class);
    private static final MethodHandle RECEIVED_FORCE_MATERIALIZE =
        virtualMethod(Received.class, "forceMaterialize", void.class);
    private static final MethodHandle RECEIVED_TAKE_PARTS =
        virtualMethod(Received.class, "takeParts", List.class);
    private static final MethodHandle RECEIVED_SET_SEND_SENDER =
        virtualMethod(Received.class, "setSendSender", void.class,
          BiFunction.class);
    private static final MethodHandle IN_CALLBACK =
        staticMethod(Socket.class, "inCallbackContext", boolean.class);
    private static final MethodHandle ENTER_CALLBACK =
        staticMethod(Socket.class, "enterCallbackContext", void.class);
    private static final MethodHandle LEAVE_CALLBACK =
        staticMethod(Socket.class, "leaveCallbackContext", void.class);
    private static final MethodHandle ROUTING_ID_FROM_TRUSTED =
        staticMethod(RoutingId.class, "fromTrusted", RoutingId.class,
          byte[].class);
    private static final MethodHandle ROUTING_ID_TRUSTED_BYTES =
        virtualMethod(RoutingId.class, "trustedBytes", byte[].class);
    private static final MethodHandle ZLINK_EXCEPTION_FROM_LAST_ERROR =
        staticMethod(ZlinkException.class, "fromLastError", ZlinkException.class,
          String.class);
    private static final MethodHandle ZLINK_EXCEPTION_FROM_ERRNO =
        staticMethod(ZlinkException.class, "fromErrno", ZlinkException.class,
          String.class, int.class);

    private InternalAccess() {
    }

    public static MemorySegment contextHandle(Context context) {
        try {
            return (MemorySegment) CONTEXT_HANDLE.invoke(context);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment discoveryHandle(Discovery discovery) {
        try {
            return (MemorySegment) DISCOVERY_HANDLE.invoke(discovery);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment socketHandle(Socket socket) {
        try {
            return (MemorySegment) SOCKET_HANDLE.invoke(socket);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment spotHandle(Spot spot) {
        try {
            return (MemorySegment) SPOT_HANDLE.invoke(spot);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment spotNodeHandle(SpotNode node) {
        try {
            return (MemorySegment) SPOT_NODE_HANDLE.invoke(node);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment spotDispatchSubject(SpotDispatchInfo info) {
        try {
            return (MemorySegment) SPOT_DISPATCH_SUBJECT.invoke(info);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject) {
        try {
            return (SpotDispatchInfo) SPOT_DISPATCH_INFO.invoke(event,
              subjectKind, subject);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    @SuppressWarnings("unchecked")
    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject,
      List<ActorPart> actorParts) {
        try {
            return (SpotDispatchInfo) SPOT_DISPATCH_INFO_ACTOR.invoke(
              event, subjectKind, subject, (List<?>) actorParts);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    @SuppressWarnings("unchecked")
    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject,
      Timer timer,
      String channelName,
      List<ActorPart> actorParts) {
        try {
            return (SpotDispatchInfo) SPOT_DISPATCH_INFO_FULL.invoke(
              event, subjectKind, subject, timer, channelName,
              (List<?>) actorParts);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Timer timerFromBorrowedHandle(MemorySegment handle) {
        try {
            return (Timer) TIMER_FROM_BORROWED_HANDLE.invoke(handle);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment messageDataSegment(Message message) {
        try {
            return (MemorySegment) MESSAGE_DATA_SEGMENT.invoke(message);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment messageDataSegment(Message message,
                                                   int knownSize) {
        try {
            return (MemorySegment) MESSAGE_DATA_SEGMENT_KNOWN.invoke(
              message, knownSize);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageCopyTo(Message message,
                                     MemorySegment destination) {
        try {
            MESSAGE_COPY_TO.invoke(message, destination);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageMoveTo(Message message,
                                     MemorySegment destination) {
        try {
            MESSAGE_MOVE_TO.invoke(message, destination);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment messageNativeHandle(Message message) {
        try {
            return (MemorySegment) MESSAGE_NATIVE_HANDLE.invoke(message);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageSetMore(Message message, boolean more) {
        try {
            MESSAGE_SET_MORE.invoke(message, more);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static boolean messageMore(Message message) {
        try {
            return (boolean) MESSAGE_MORE.invoke(message);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageFinishReceive(Message message, boolean more) {
        try {
            MESSAGE_FINISH_RECEIVE.invoke(message, more);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Object messageTransferTo(Message message,
                                           MemorySegment destination) {
        try {
            return MESSAGE_TRANSFER_TO.invoke(message, destination);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageRestoreFromNative(Message message,
                                                MemorySegment source,
                                                boolean moreFlag,
                                                Object anchor) {
        try {
            MESSAGE_RESTORE_FROM_NATIVE.invoke(message, source, moreFlag,
              anchor);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Message messageSharedCopyOf(Message message) {
        try {
            return (Message) MESSAGE_SHARED_COPY_OF.invoke(message);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Message[] messageFromMsgVector(MemorySegment partsAddr,
                                                 long count) {
        try {
            return (Message[]) MESSAGE_FROM_MSG_VECTOR.invoke(partsAddr,
              count);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Message[] messageFromOwnedMsgVector(MemorySegment partsAddr,
                                                      long count) {
        try {
            return (Message[]) MESSAGE_FROM_OWNED_MSG_VECTOR.invoke(
              partsAddr, count);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Message[] messageFromOwnedMsgVectorShared(
      MemorySegment partsAddr,
      long count) {
        try {
            return (Message[]) MESSAGE_FROM_OWNED_MSG_VECTOR_SHARED
              .invoke(partsAddr, count);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Received received(RoutingId routingId, Message[] parts) {
        try {
            return (Received) RECEIVED.invoke(routingId, parts);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    @SuppressWarnings("unchecked")
    public static Received received(RoutingId routingId,
                                    RoutingId spotRid,
                                    Message[] parts,
                                    long requestSeq,
                                    boolean hasRequestSeq,
                                    BiConsumer<List<Message>, SendFlags> replySender) {
        try {
            return (Received) RECEIVED_WITH_REPLY.invoke(routingId,
              spotRid, parts, true, requestSeq, hasRequestSeq,
              (BiConsumer<?, ?>) replySender);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    @SuppressWarnings("unchecked")
    public static Received receivedLazy(byte[] routingIdBytes,
                                        byte[] spotRidBytes,
                                        Message firstPart,
                                        ReceivedPartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        try {
            return (Received) RECEIVED_LAZY_BYTES.invoke(routingIdBytes,
              spotRidBytes, firstPart, cursor, requestSeq, hasRequestSeq,
              (BiConsumer<?, ?>) replySender, onTerminalState);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    @SuppressWarnings("unchecked")
    public static Received receivedLazy(RoutingId routingId,
                                        RoutingId spotRid,
                                        Message firstPart,
                                        ReceivedPartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        try {
            return (Received) RECEIVED_LAZY_RIDS.invoke(routingId,
              spotRid, firstPart, cursor, requestSeq, hasRequestSeq,
              (BiConsumer<?, ?>) replySender, onTerminalState);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static TopicMessage topicMessage(RoutingId routingId,
                                            String topicId,
                                            Message[] parts) {
        try {
            return (TopicMessage) TOPIC_MESSAGE.invoke(routingId,
              topicId, parts);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void receivedForceMaterialize(Received received) {
        try {
            RECEIVED_FORCE_MATERIALIZE.invoke(received);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    @SuppressWarnings("unchecked")
    public static List<Message> receivedTakeParts(Received received) {
        try {
            return (List<Message>) RECEIVED_TAKE_PARTS.invoke(received);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void receivedSetSendSender(Received received,
                                             BiFunction<List<Message>, SendFlags,
                                                 Boolean> sendSender) {
        try {
            RECEIVED_SET_SEND_SENDER.invoke(received, sendSender);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static boolean inCallback() {
        try {
            return (boolean) IN_CALLBACK.invoke();
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void enterCallback() {
        try {
            ENTER_CALLBACK.invoke();
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void leaveCallback() {
        try {
            LEAVE_CALLBACK.invoke();
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static RoutingId routingIdFromTrusted(byte[] value) {
        try {
            return (RoutingId) ROUTING_ID_FROM_TRUSTED.invoke(value);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static byte[] routingIdTrustedBytes(RoutingId routingId) {
        try {
            return (byte[]) ROUTING_ID_TRUSTED_BYTES.invoke(routingId);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static ZlinkException zlinkExceptionFromLastError(String operation) {
        try {
            return (ZlinkException) ZLINK_EXCEPTION_FROM_LAST_ERROR.invoke(
              operation);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static ZlinkException zlinkExceptionFromErrno(String operation,
                                                         int errno) {
        try {
            return (ZlinkException) ZLINK_EXCEPTION_FROM_ERRNO.invoke(
              operation, errno);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    private static MethodHandle constructor(Class<?> type,
                                            Class<?>... parameterTypes) {
        try {
            return lookup(type).findConstructor(type,
              MethodType.methodType(void.class, parameterTypes));
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static MethodHandle staticMethod(Class<?> type,
                                             String name,
                                             Class<?> returnType,
                                             Class<?>... parameterTypes) {
        try {
            return lookup(type).findStatic(type, name,
              MethodType.methodType(returnType, parameterTypes));
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static MethodHandle virtualMethod(Class<?> type,
                                              String name,
                                              Class<?> returnType,
                                              Class<?>... parameterTypes) {
        try {
            return lookup(type).findVirtual(type, name,
              MethodType.methodType(returnType, parameterTypes));
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static MethodHandles.Lookup lookup(Class<?> type)
      throws IllegalAccessException {
        return MethodHandles.privateLookupIn(type, MethodHandles.lookup());
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
