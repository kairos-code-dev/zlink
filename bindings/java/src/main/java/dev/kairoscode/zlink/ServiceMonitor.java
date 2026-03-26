/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Objects;

/** Receives service-layer monitor events from discovery, registry, or spot APIs. */
public final class ServiceMonitor implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_SERVICE_MONITOR_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    private MemorySegment handle;
    private ServiceMonitorHandler eventHandler;
    private Arena callbackArena;
    private MemorySegment callbackStub = MemorySegment.NULL;
    private volatile RuntimeException callbackFailure;

    public ServiceMonitor(MemorySegment handle) {
        this.handle = handle;
    }

    /** Installs a direct service monitor callback. */
    public void onEvent(ServiceMonitorHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
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
        } finally {
            if (!success) {
                closeArena(arena);
            }
        }
    }

    /** Blocks until the next service event is available. */
    public ServiceEvent recv() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment event = arena.allocate(NativeLayouts.SERVICE_EVENT_LAYOUT);
            int rc = Native.serviceMonitorRecv(handle, event);
            if (rc != 0) {
                throw ZlinkException.fromLastError("zlink_service_monitor_recv");
            }
            return ServiceEvent.fromNative(event);
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
        if (handler == null)
            return;
        try {
            handler.onEvent(ServiceEvent.fromNative(event.reinterpret(
              NativeLayouts.SERVICE_EVENT_LAYOUT.byteSize())));
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
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
}
