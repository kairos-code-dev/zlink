/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.SpotDispatchInfo;
import dev.kairoscode.zlink.SpotDispatchSubjectKind;
import dev.kairoscode.zlink.Timer;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.spot.ActorPart;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.List;
import java.util.function.BiConsumer;

/**
 * Non-exported bridge for package-private root-package internals.
 */
public final class InternalAccess {
    private static final Class<?> ROOT_BRIDGE = rootBridgeClass();
    private static final MethodHandles.Lookup ROOT_LOOKUP = rootLookup();

    private static final MethodHandle CONTEXT_HANDLE =
        staticMethod("contextHandle", MemorySegment.class, Context.class);
    private static final MethodHandle DISCOVERY_HANDLE =
        staticMethod("discoveryHandle", MemorySegment.class, Discovery.class);
    private static final MethodHandle SOCKET_HANDLE =
        staticMethod("socketHandle", MemorySegment.class, Socket.class);
    private static final MethodHandle SPOT_HANDLE =
        staticMethod("spotHandle", MemorySegment.class, Spot.class);
    private static final MethodHandle SPOT_NODE_HANDLE =
        staticMethod("spotNodeHandle", MemorySegment.class, SpotNode.class);
    private static final MethodHandle SPOT_DISPATCH_SUBJECT =
        staticMethod("spotDispatchSubject", MemorySegment.class,
          SpotDispatchInfo.class);
    private static final MethodHandle SPOT_DISPATCH_INFO =
        staticMethod("spotDispatchInfo", SpotDispatchInfo.class,
          SpotDispatchEvent.class, SpotDispatchSubjectKind.class,
          MemorySegment.class);
    private static final MethodHandle SPOT_DISPATCH_INFO_ACTOR =
        staticMethod("spotDispatchInfo", SpotDispatchInfo.class,
          SpotDispatchEvent.class, SpotDispatchSubjectKind.class,
          MemorySegment.class, List.class);
    private static final MethodHandle SPOT_DISPATCH_INFO_FULL =
        staticMethod("spotDispatchInfo", SpotDispatchInfo.class,
          SpotDispatchEvent.class, SpotDispatchSubjectKind.class,
          MemorySegment.class, Timer.class, String.class, List.class);
    private static final MethodHandle TIMER_FROM_BORROWED_HANDLE =
        staticMethod("timerFromBorrowedHandle", Timer.class,
          MemorySegment.class);
    private static final MethodHandle MESSAGE_DATA_SEGMENT =
        staticMethod("messageDataSegment", MemorySegment.class, Message.class);
    private static final MethodHandle MESSAGE_DATA_SEGMENT_KNOWN =
        staticMethod("messageDataSegment", MemorySegment.class, Message.class,
          int.class);
    private static final MethodHandle MESSAGE_COPY_TO =
        staticMethod("messageCopyTo", void.class, Message.class,
          MemorySegment.class);
    private static final MethodHandle MESSAGE_MOVE_TO =
        staticMethod("messageMoveTo", void.class, Message.class,
          MemorySegment.class);
    private static final MethodHandle MESSAGE_NATIVE_HANDLE =
        staticMethod("messageNativeHandle", MemorySegment.class,
          Message.class);
    private static final MethodHandle MESSAGE_SET_MORE =
        staticMethod("messageSetMore", void.class, Message.class,
          boolean.class);
    private static final MethodHandle MESSAGE_MORE =
        staticMethod("messageMore", boolean.class, Message.class);
    private static final MethodHandle MESSAGE_FINISH_RECEIVE =
        staticMethod("messageFinishReceive", void.class, Message.class,
          boolean.class);
    private static final MethodHandle MESSAGE_TRANSFER_TO =
        staticMethod("messageTransferTo", Object.class, Message.class,
          MemorySegment.class);
    private static final MethodHandle MESSAGE_RESTORE_FROM_NATIVE =
        staticMethod("messageRestoreFromNative", void.class, Message.class,
          MemorySegment.class, boolean.class, Object.class);
    private static final MethodHandle MESSAGE_SHARED_COPY_OF =
        staticMethod("messageSharedCopyOf", Message.class, Message.class);
    private static final MethodHandle MESSAGE_FROM_MSG_VECTOR =
        staticMethod("messageFromMsgVector", Message[].class,
          MemorySegment.class, long.class);
    private static final MethodHandle MESSAGE_FROM_OWNED_MSG_VECTOR =
        staticMethod("messageFromOwnedMsgVector", Message[].class,
          MemorySegment.class, long.class);
    private static final MethodHandle MESSAGE_FROM_OWNED_MSG_VECTOR_SHARED =
        staticMethod("messageFromOwnedMsgVectorShared", Message[].class,
          MemorySegment.class, long.class);
    private static final MethodHandle RECEIVED =
        staticMethod("received", Received.class, RoutingId.class,
          Message[].class);
    private static final MethodHandle RECEIVED_WITH_REPLY =
        staticMethod("received", Received.class, RoutingId.class,
          RoutingId.class, Message[].class, long.class, boolean.class,
          BiConsumer.class);
    private static final MethodHandle RECEIVED_LAZY_BYTES =
        staticMethod("receivedLazy", Received.class, byte[].class,
          byte[].class, Message.class, ReceivedPartCursor.class, long.class,
          boolean.class, BiConsumer.class, Runnable.class);
    private static final MethodHandle RECEIVED_LAZY_RIDS =
        staticMethod("receivedLazy", Received.class, RoutingId.class,
          RoutingId.class, Message.class, ReceivedPartCursor.class,
          long.class, boolean.class, BiConsumer.class, Runnable.class);
    private static final MethodHandle TOPIC_MESSAGE =
        staticMethod("topicMessage", TopicMessage.class, RoutingId.class,
          String.class, String.class, Message[].class);
    private static final MethodHandle RECEIVED_FORCE_MATERIALIZE =
        staticMethod("receivedForceMaterialize", void.class, Received.class);
    private static final MethodHandle RECEIVED_TAKE_PARTS =
        staticMethod("receivedTakeParts", List.class, Received.class);
    private static final MethodHandle IN_CALLBACK =
        staticMethod("inCallback", boolean.class);
    private static final MethodHandle ENTER_CALLBACK =
        staticMethod("enterCallback", void.class);
    private static final MethodHandle LEAVE_CALLBACK =
        staticMethod("leaveCallback", void.class);
    private static final MethodHandle ROUTING_ID_FROM_TRUSTED =
        staticMethod("routingIdFromTrusted", RoutingId.class, byte[].class);
    private static final MethodHandle ROUTING_ID_TRUSTED_BYTES =
        staticMethod("routingIdTrustedBytes", byte[].class, RoutingId.class);
    private static final MethodHandle ZLINK_EXCEPTION_FROM_LAST_ERROR =
        staticMethod("zlinkExceptionFromLastError", ZlinkException.class,
          String.class);
    private static final MethodHandle ZLINK_EXCEPTION_FROM_ERRNO =
        staticMethod("zlinkExceptionFromErrno", ZlinkException.class,
          String.class, int.class);

    private InternalAccess() {
    }

    public static MemorySegment contextHandle(Context context) {
        try {
            return (MemorySegment) CONTEXT_HANDLE.invokeExact(context);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment discoveryHandle(Discovery discovery) {
        try {
            return (MemorySegment) DISCOVERY_HANDLE.invokeExact(discovery);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment socketHandle(Socket socket) {
        try {
            return (MemorySegment) SOCKET_HANDLE.invokeExact(socket);
        } catch (Throwable t) {
            throw unchecked(t);
        }
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
        try {
            return (MemorySegment) SPOT_DISPATCH_SUBJECT.invokeExact(info);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject) {
        try {
            return (SpotDispatchInfo) SPOT_DISPATCH_INFO.invokeExact(event,
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
            return (SpotDispatchInfo) SPOT_DISPATCH_INFO_ACTOR.invokeExact(
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
            return (SpotDispatchInfo) SPOT_DISPATCH_INFO_FULL.invokeExact(
              event, subjectKind, subject, timer, channelName,
              (List<?>) actorParts);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Timer timerFromBorrowedHandle(MemorySegment handle) {
        try {
            return (Timer) TIMER_FROM_BORROWED_HANDLE.invokeExact(handle);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment messageDataSegment(Message message) {
        try {
            return (MemorySegment) MESSAGE_DATA_SEGMENT.invokeExact(message);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment messageDataSegment(Message message,
                                                   int knownSize) {
        try {
            return (MemorySegment) MESSAGE_DATA_SEGMENT_KNOWN.invokeExact(
              message, knownSize);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageCopyTo(Message message,
                                     MemorySegment destination) {
        try {
            MESSAGE_COPY_TO.invokeExact(message, destination);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageMoveTo(Message message,
                                     MemorySegment destination) {
        try {
            MESSAGE_MOVE_TO.invokeExact(message, destination);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static MemorySegment messageNativeHandle(Message message) {
        try {
            return (MemorySegment) MESSAGE_NATIVE_HANDLE.invokeExact(message);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageSetMore(Message message, boolean more) {
        try {
            MESSAGE_SET_MORE.invokeExact(message, more);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static boolean messageMore(Message message) {
        try {
            return (boolean) MESSAGE_MORE.invokeExact(message);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageFinishReceive(Message message, boolean more) {
        try {
            MESSAGE_FINISH_RECEIVE.invokeExact(message, more);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Object messageTransferTo(Message message,
                                           MemorySegment destination) {
        try {
            return MESSAGE_TRANSFER_TO.invokeExact(message, destination);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void messageRestoreFromNative(Message message,
                                                MemorySegment source,
                                                boolean moreFlag,
                                                Object anchor) {
        try {
            MESSAGE_RESTORE_FROM_NATIVE.invokeExact(message, source, moreFlag,
              anchor);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Message messageSharedCopyOf(Message message) {
        try {
            return (Message) MESSAGE_SHARED_COPY_OF.invokeExact(message);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Message[] messageFromMsgVector(MemorySegment partsAddr,
                                                 long count) {
        try {
            return (Message[]) MESSAGE_FROM_MSG_VECTOR.invokeExact(partsAddr,
              count);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Message[] messageFromOwnedMsgVector(MemorySegment partsAddr,
                                                      long count) {
        try {
            return (Message[]) MESSAGE_FROM_OWNED_MSG_VECTOR.invokeExact(
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
              .invokeExact(partsAddr, count);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static Received received(RoutingId routingId, Message[] parts) {
        try {
            return (Received) RECEIVED.invokeExact(routingId, parts);
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
            return (Received) RECEIVED_WITH_REPLY.invokeExact(routingId,
              spotRid, parts, requestSeq, hasRequestSeq,
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
            return (Received) RECEIVED_LAZY_BYTES.invokeExact(routingIdBytes,
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
            return (Received) RECEIVED_LAZY_RIDS.invokeExact(routingId,
              spotRid, firstPart, cursor, requestSeq, hasRequestSeq,
              (BiConsumer<?, ?>) replySender, onTerminalState);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static TopicMessage topicMessage(RoutingId routingId,
                                            String serviceName,
                                            String topicId,
                                            Message[] parts) {
        try {
            return (TopicMessage) TOPIC_MESSAGE.invokeExact(routingId,
              serviceName, topicId, parts);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void receivedForceMaterialize(Received received) {
        try {
            RECEIVED_FORCE_MATERIALIZE.invokeExact(received);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    @SuppressWarnings("unchecked")
    public static List<Message> receivedTakeParts(Received received) {
        try {
            return (List<Message>) RECEIVED_TAKE_PARTS.invokeExact(received);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static boolean inCallback() {
        try {
            return (boolean) IN_CALLBACK.invokeExact();
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void enterCallback() {
        try {
            ENTER_CALLBACK.invokeExact();
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static void leaveCallback() {
        try {
            LEAVE_CALLBACK.invokeExact();
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static RoutingId routingIdFromTrusted(byte[] value) {
        try {
            return (RoutingId) ROUTING_ID_FROM_TRUSTED.invokeExact(value);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static byte[] routingIdTrustedBytes(RoutingId routingId) {
        try {
            return (byte[]) ROUTING_ID_TRUSTED_BYTES.invokeExact(routingId);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static ZlinkException zlinkExceptionFromLastError(String operation) {
        try {
            return (ZlinkException) ZLINK_EXCEPTION_FROM_LAST_ERROR.invokeExact(
              operation);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    public static ZlinkException zlinkExceptionFromErrno(String operation,
                                                         int errno) {
        try {
            return (ZlinkException) ZLINK_EXCEPTION_FROM_ERRNO.invokeExact(
              operation, errno);
        } catch (Throwable t) {
            throw unchecked(t);
        }
    }

    private static Class<?> rootBridgeClass() {
        try {
            return Class.forName("dev.kairoscode.zlink.InternalAccess");
        } catch (ClassNotFoundException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static MethodHandles.Lookup rootLookup() {
        try {
            return MethodHandles.privateLookupIn(ROOT_BRIDGE,
              MethodHandles.lookup());
        } catch (IllegalAccessException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static MethodHandle staticMethod(String name, Class<?> returnType,
                                             Class<?>... parameterTypes) {
        try {
            return ROOT_LOOKUP.findStatic(ROOT_BRIDGE, name,
              MethodType.methodType(returnType, parameterTypes));
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
