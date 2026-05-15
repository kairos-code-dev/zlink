/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.Native;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import systems.zlink.runtime.nativebridge.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

public final class MonitorSocket implements AutoCloseable {
    public static final SocketMonitorHandler IGNORE_HANDLER = event -> {
    };

    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_MONITOR_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    private MemorySegment handle;
    private final boolean own;
    private SocketMonitorHandler eventHandler;
    private Arena callbackArena;
    private MemorySegment callbackStub = MemorySegment.NULL;
    private volatile ExecutorService callbackExecutor;
    private volatile RuntimeException callbackFailure;

    MonitorSocket(MemorySegment handle, boolean own) {
        this.handle = handle;
        this.own = own;
    }

    public void onEvent(SocketMonitorHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = newCallbackExecutor();
        ExecutorService previousExecutor = callbackExecutor;
        callbackExecutor = executor;
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
          "handleEventCallback", MethodType.methodType(void.class,
            MemorySegment.class, MemorySegment.class)),
          FD_MONITOR_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.monitorHandler(handle, stub, MemorySegment.NULL);
            if (rc != 0) {
                throw ZlinkException.fromLastError("zlink_socket_monitor_handler");
            }
            success = true;
            closeArena(callbackArena);
            callbackArena = arena;
            callbackStub = stub;
            eventHandler = handler;
            shutdownExecutor(previousExecutor);
        } finally {
            if (!success) {
                callbackExecutor = previousExecutor;
                closeArena(arena);
                shutdownExecutor(executor);
            }
        }
    }

    public MonitorEvent recv() {
        return recv(RecvFlags.NONE);
    }

    public MonitorEvent recv(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        try {
            return Native.monitorRecv(handle, flags.value());
        } catch (RecvException ex) {
            if (flags == RecvFlags.DONT_WAIT
                && ex.getResult() == RecvResult.NO_DATA) {
                return null;
            }
            throw ex;
        }
    }

    Optional<MonitorEvent> recvNoWait() {
        ensureOpen();
        return Optional.ofNullable(recv(RecvFlags.DONT_WAIT));
    }

    public MonitorSnapshot snapshot() {
        try (java.lang.foreign.Arena arena = java.lang.foreign.Arena.ofConfined()) {
            MemorySegment out = arena.allocate(
              systems.zlink.runtime.nativebridge.NativeLayouts.MONITOR_SNAPSHOT_LAYOUT);
            int rc = Native.monitorSnapshot(handle, out);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_monitor_snapshot");
            return MonitorSnapshot.fromNative(out);
        }
    }

    MemorySegment handle() {
        return handle;
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        eventHandler = null;
        callbackFailure = null;
        shutdownExecutor(callbackExecutor);
        callbackExecutor = null;
        closeArena(callbackArena);
        callbackArena = null;
        callbackStub = MemorySegment.NULL;
        if (own) {
            Native.monitorClose(handle);
        }
        handle = MemorySegment.NULL;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("monitor socket is closed");
        ensureNoCallbackFailure();
    }

    private void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null)
            throw failure;
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(MonitorSocket.class, name,
              type).bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name,
              ex);
        }
    }

    private void handleEventCallback(MemorySegment event,
                                     MemorySegment userdata) {
        SocketMonitorHandler handler = eventHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null)
            return;
        try {
            MemorySegment evt = event.reinterpret(
              NativeLayouts.MONITOR_EVENT_LAYOUT.byteSize());
            long eventValue = evt.get(ValueLayout.JAVA_LONG,
              NativeLayouts.MONITOR_EVENT_OFFSET);
            long value = evt.get(ValueLayout.JAVA_LONG,
              NativeLayouts.MONITOR_VALUE_OFFSET);
            int routingSize = evt.get(ValueLayout.JAVA_BYTE,
              NativeLayouts.MONITOR_ROUTING_OFFSET
                + NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
            byte[] routing = new byte[routingSize];
            if (routingSize > 0) {
                MemorySegment.copy(evt,
                  NativeLayouts.MONITOR_ROUTING_OFFSET
                    + NativeLayouts.ROUTING_ID_DATA_OFFSET,
                  MemorySegment.ofArray(routing), 0, routingSize);
            }
            String local = NativeHelpers.fromCString(evt.asSlice(
              NativeLayouts.MONITOR_LOCAL_OFFSET, 256), 256);
            String remote = NativeHelpers.fromCString(evt.asSlice(
              NativeLayouts.MONITOR_REMOTE_OFFSET, 256), 256);
            MonitorEvent monitorEvent = new MonitorEvent(
              MonitorEventType.fromValue(eventValue), value,
              routingSize == 0 ? Optional.empty()
                : Optional.of(RoutingId.fromTrusted(routing)),
              local, remote);
            executor.execute(() -> dispatchEvent(handler, monitorEvent));
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchEvent(SocketMonitorHandler handler,
                               MonitorEvent event) {
        Socket.enterCallbackContext();
        try {
            handler.onEvent(event);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            Socket.leaveCallbackContext();
        }
    }

    private void recordCallbackFailure(RuntimeException failure) {
        callbackFailure = failure;
        Thread current = Thread.currentThread();
        Thread.UncaughtExceptionHandler uncaught =
          current.getUncaughtExceptionHandler();
        if (uncaught != null)
            uncaught.uncaughtException(current, failure);
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    private static ExecutorService newCallbackExecutor() {
        return Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "zlink-monitor-callback");
            thread.setDaemon(true);
            return thread;
        });
    }

    private static void shutdownExecutor(ExecutorService executor) {
        if (executor != null)
            executor.shutdown();
    }
}
