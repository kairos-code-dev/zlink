/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.service.registry.ServiceEvent;
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

/** Receives service-layer monitor events from discovery, registry, or spot APIs. */
public final class ServiceMonitor implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_SERVICE_MONITOR_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    private MemorySegment handle;
    private ServiceMonitorHandler eventHandler;
    private Arena callbackArena;
    private MemorySegment callbackStub = MemorySegment.NULL;
    private volatile ExecutorService callbackExecutor;
    private volatile RuntimeException callbackFailure;

    ServiceMonitor(MemorySegment handle) {
        this.handle = handle;
    }

    /**
     * Installs a service monitor callback delivered on a managed Java thread.
     */
    public void onEvent(ServiceMonitorHandler handler) {
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
          FD_SERVICE_MONITOR_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.serviceMonitorHandler(handle, stub,
              MemorySegment.NULL);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_service_monitor_handler");
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

    /** Blocks until the next service event is available. */
    public ServiceEvent recv() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment event = arena.allocate(NativeLayouts.SERVICE_EVENT_LAYOUT);
            int rc = Native.serviceMonitorRecv(handle, event,
              RecvFlags.NONE.value());
            if (rc != 0) {
                try {
                    throw new RecvException(RecvResult.fromValue(rc),
                      Native.errno());
                } catch (IllegalArgumentException ex) {
                    throw ZlinkException.fromLastError("zlink_service_monitor_recv");
                }
            }
            return ServiceEvent.fromNative(event);
        }
    }

    Optional<ServiceEvent> tryRecv() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment event = arena.allocate(NativeLayouts.SERVICE_EVENT_LAYOUT);
            int rc = Native.serviceMonitorRecv(handle, event,
              RecvFlags.DONT_WAIT.value());
            if (rc == 0)
                return Optional.of(ServiceEvent.fromNative(event));
            try {
                RecvResult result = RecvResult.fromValue(rc);
                if (result == RecvResult.NO_DATA || result == RecvResult.BUSY)
                    return Optional.empty();
                throw new RecvException(result, Native.errno());
            } catch (IllegalArgumentException ex) {
                throw ZlinkException.fromLastError("zlink_service_monitor_recv");
            }
        } catch (RecvException ex) {
            if (ex.getResult() == RecvResult.NO_DATA)
                return Optional.empty();
            throw ex;
        }
    }

    /** Returns the current service monitor snapshot. */
    public MonitorSnapshot snapshot() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.MONITOR_SNAPSHOT_LAYOUT);
            int rc = Native.monitorSnapshot(handle, out);
            if (rc != 0) {
                throw ZlinkException.fromLastError("zlink_monitor_snapshot");
            }
            return MonitorSnapshot.fromNative(out);
        }
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
        Native.monitorClose(handle);
        handle = MemorySegment.NULL;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("service monitor is closed");
        ensureNoCallbackFailure();
    }

    private void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null)
            throw failure;
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(ServiceMonitor.class,
              name, type).bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name,
              ex);
        }
    }

    private void handleEventCallback(MemorySegment event,
                                     MemorySegment userdata) {
        ServiceMonitorHandler handler = eventHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null)
            return;
        try {
            ServiceEvent serviceEvent = ServiceEvent.fromNative(event.reinterpret(
              NativeLayouts.SERVICE_EVENT_LAYOUT.byteSize()));
            executor.execute(() -> dispatchEvent(handler, serviceEvent));
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchEvent(ServiceMonitorHandler handler,
                               ServiceEvent event) {
        if (handle == null || handle.address() == 0)
            return;
        SocketCore.enterCallback();
        try {
            handler.onEvent(event);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            SocketCore.leaveCallback();
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
            Thread thread = new Thread(runnable,
              "zlink-service-monitor-callback");
            thread.setDaemon(true);
            return thread;
        });
    }

    private static void shutdownExecutor(ExecutorService executor) {
        if (executor != null)
            executor.shutdown();
    }
}
