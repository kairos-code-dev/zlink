/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.service.discovery.Discovery;
import java.lang.foreign.MemorySegment;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;

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
    private static final Method MESSAGE_FROM_MSG_VECTOR =
        method(Message.class, "fromMsgVector", MemorySegment.class, long.class);
    private static final Method MESSAGE_FROM_OWNED_MSG_VECTOR =
        method(Message.class, "fromOwnedMsgVector", MemorySegment.class,
            long.class);

    private InternalAccess() {}

    public static MemorySegment contextHandle(Context context) {
        return (MemorySegment) invoke(CONTEXT_HANDLE, context);
    }

    public static MemorySegment discoveryHandle(Discovery discovery) {
        return (MemorySegment) invoke(DISCOVERY_HANDLE, discovery);
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
