/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.ServiceMonitor;
import java.lang.foreign.MemorySegment;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.spot.Spot;
import java.util.List;
import java.util.function.BiConsumer;

/**
 * Reflection bridge for package-private binding internals used by subpackages.
 *
 * <p>This keeps native handles and FFI-only helpers out of the canonical
 * public surface while still allowing service subpackages to reach the
 * package-private implementation hooks they need.
 */
public final class InternalAccess {
    private static final Method CONTEXT_HANDLE =
        method(Context.class, "handle");
    private static final Method DISCOVERY_HANDLE =
        method(Discovery.class, "handle");
    private static final Method SPOT_HANDLE =
        method(Spot.class, "handle");
    private static final Constructor<ServiceMonitor> SERVICE_MONITOR_CTOR =
        constructor(ServiceMonitor.class, MemorySegment.class);
    private static final Method MESSAGE_DATA_SEGMENT =
        method(Message.class, "dataSegment");
    private static final Method MESSAGE_DATA_SEGMENT_WITH_SIZE =
        method(Message.class, "dataSegment", int.class);
    private static final Method MESSAGE_COPY_TO =
        method(Message.class, "copyTo", MemorySegment.class);
    private static final Method MESSAGE_MOVE_TO =
        method(Message.class, "moveTo", MemorySegment.class);
    private static final Method MESSAGE_SHARED_COPY_OF =
        method(Message.class, "sharedCopyOf", Message.class);
    private static final Method MESSAGE_FROM_MSG_VECTOR =
        method(Message.class, "fromMsgVector", MemorySegment.class, long.class);
    private static final Method MESSAGE_FROM_OWNED_MSG_VECTOR =
        method(Message.class, "fromOwnedMsgVector", MemorySegment.class,
            long.class);
    private static final Method MESSAGE_FROM_OWNED_MSG_VECTOR_SHARED =
        method(Message.class, "fromOwnedMsgVectorShared", MemorySegment.class,
            long.class);
    private static final Constructor<Received> RECEIVED_TRUSTED_CTOR =
        constructor(Received.class, dev.kairoscode.zlink.RoutingId.class,
            dev.kairoscode.zlink.RoutingId.class, Message[].class,
            boolean.class, long.class, boolean.class, BiConsumer.class);
    private static final Method SOCKET_CORE_IN_CALLBACK =
        method(classForName("dev.kairoscode.zlink.SocketCore"), "inCallback");
    private static final Method SOCKET_CORE_ENTER_CALLBACK =
        method(classForName("dev.kairoscode.zlink.SocketCore"), "enterCallback");
    private static final Method SOCKET_CORE_LEAVE_CALLBACK =
        method(classForName("dev.kairoscode.zlink.SocketCore"), "leaveCallback");
    private static final Method ROUTING_ID_FROM_TRUSTED =
        method(RoutingId.class, "fromTrusted", byte[].class);
    private static final Method ROUTING_ID_TRUSTED_BYTES =
        method(RoutingId.class, "trustedBytes");

    private InternalAccess() {}

    public static MemorySegment contextHandle(Context context) {
        return (MemorySegment) invoke(CONTEXT_HANDLE, context);
    }

    public static MemorySegment discoveryHandle(Discovery discovery) {
        return (MemorySegment) invoke(DISCOVERY_HANDLE, discovery);
    }

    public static MemorySegment spotHandle(Spot spot) {
        return (MemorySegment) invoke(SPOT_HANDLE, spot);
    }

    public static ServiceMonitor serviceMonitor(MemorySegment handle) {
        try {
            return SERVICE_MONITOR_CTOR.newInstance(handle);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException(
                "failed to create internal service monitor",
                ex);
        }
    }

    public static MemorySegment messageDataSegment(Message message) {
        return (MemorySegment) invoke(MESSAGE_DATA_SEGMENT, message);
    }

    public static MemorySegment messageDataSegment(Message message,
                                                   int knownSize) {
        return (MemorySegment) invoke(MESSAGE_DATA_SEGMENT_WITH_SIZE, message,
            knownSize);
    }

    public static void messageCopyTo(Message message, MemorySegment destination) {
        invoke(MESSAGE_COPY_TO, message, destination);
    }

    public static void messageMoveTo(Message message, MemorySegment destination) {
        invoke(MESSAGE_MOVE_TO, message, destination);
    }

    public static Message messageSharedCopyOf(Message message) {
        return (Message) invoke(MESSAGE_SHARED_COPY_OF, null, message);
    }

    public static Message[] messageFromMsgVector(MemorySegment partsAddr,
                                                 long count) {
        return (Message[]) invoke(MESSAGE_FROM_MSG_VECTOR, null, partsAddr,
            count);
    }

    public static Message[] messageFromOwnedMsgVector(MemorySegment partsAddr,
                                                      long count) {
        return (Message[]) invoke(MESSAGE_FROM_OWNED_MSG_VECTOR, null,
            partsAddr, count);
    }

    public static Message[] messageFromOwnedMsgVectorShared(
                                                  MemorySegment partsAddr,
                                                  long count) {
        return (Message[]) invoke(MESSAGE_FROM_OWNED_MSG_VECTOR_SHARED, null,
            partsAddr, count);
    }

    public static Received received(dev.kairoscode.zlink.RoutingId routingId,
                                    Message[] parts) {
        return received(routingId, null, parts, 0L, false, null);
    }

    public static Received received(dev.kairoscode.zlink.RoutingId routingId,
                                    dev.kairoscode.zlink.RoutingId spotRid,
                                    Message[] parts,
                                    long requestSeq,
                                    boolean hasRequestSeq,
                                    BiConsumer<List<Message>, SendFlags> replySender) {
        try {
            return RECEIVED_TRUSTED_CTOR.newInstance(routingId, spotRid, parts,
              true, requestSeq, hasRequestSeq, replySender);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException(
                "failed to create trusted Received", ex);
        }
    }

    public static boolean inCallback() {
        return (boolean) invoke(SOCKET_CORE_IN_CALLBACK, null);
    }

    public static void enterCallback() {
        invoke(SOCKET_CORE_ENTER_CALLBACK, null);
    }

    public static void leaveCallback() {
        invoke(SOCKET_CORE_LEAVE_CALLBACK, null);
    }

    public static RoutingId routingIdFromTrusted(byte[] value) {
        return (RoutingId) invoke(ROUTING_ID_FROM_TRUSTED, null, value);
    }

    public static byte[] routingIdTrustedBytes(RoutingId routingId) {
        return (byte[]) invoke(ROUTING_ID_TRUSTED_BYTES, routingId);
    }

    private static Class<?> classForName(String name) {
        try {
            return Class.forName(name);
        } catch (ClassNotFoundException ex) {
            throw new IllegalStateException(
                "failed to bind internal class " + name, ex);
        }
    }

    private static Method method(Class<?> owner, String name,
                                 Class<?>... parameterTypes) {
        try {
            Method method = owner.getDeclaredMethod(name, parameterTypes);
            method.setAccessible(true);
            return method;
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException(
                "failed to bind internal method " + owner.getName() + "." + name,
                ex);
        }
    }

    private static <T> Constructor<T> constructor(Class<T> owner,
                                                  Class<?>... parameterTypes) {
        try {
            Constructor<T> ctor = owner.getDeclaredConstructor(parameterTypes);
            ctor.setAccessible(true);
            return ctor;
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException(
                "failed to bind internal constructor " + owner.getName(),
                ex);
        }
    }

    private static Object invoke(Method method, Object target, Object... args) {
        try {
            return method.invoke(target, args);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException(
                "failed to invoke internal method " + method.getName(),
                ex);
        }
    }
}
