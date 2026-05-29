/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.sockets.SendReadyHandler;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.RuntimeResources;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Objects;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;

final class SpotSendReadySupport implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_SEND_READY_CALLBACK =
        FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    private SendReadyHandler handler;
    private Arena callbackArena;
    private MemorySegment callbackStub = MemorySegment.NULL;
    private volatile ExecutorService callbackExecutor;
    private volatile RuntimeException callbackFailure;

    void install(MemorySegment spotHandle, SendReadyHandler nextHandler) {
        Objects.requireNonNull(spotHandle, "spotHandle");
        Objects.requireNonNull(nextHandler, "handler");
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = RuntimeResources.daemonSingleThreadExecutor(
                "zlink-spot-callback");
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleSendReadyCallback", MethodType.methodType(void.class,
                MemorySegment.class, MemorySegment.class)),
            FD_SEND_READY_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.sendReadyHandler(spotHandle, stub,
                MemorySegment.NULL);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                    "zlink_send_ready_handler");
            }
            success = true;
            RuntimeResources.closeArena(callbackArena);
            callbackArena = arena;
            callbackStub = stub;
            handler = nextHandler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    RuntimeResources.shutdownExecutor(executor);
                }
                RuntimeResources.closeArena(arena);
            }
        }
    }

    void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null)
            throw failure;
    }

    @Override
    public void close() {
        ExecutorService executor = callbackExecutor;
        Arena arena = callbackArena;
        handler = null;
        callbackFailure = null;
        callbackExecutor = null;
        callbackArena = null;
        callbackStub = MemorySegment.NULL;
        RuntimeResources.shutdownExecutor(executor);
        RuntimeResources.closeArena(arena);
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(
                SpotSendReadySupport.class, name, type).bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name,
                ex);
        }
    }

    private void handleSendReadyCallback(MemorySegment subject,
                                         MemorySegment userdata) {
        SendReadyHandler currentHandler = handler;
        ExecutorService executor = callbackExecutor;
        if (currentHandler == null || executor == null)
            return;
        try {
            executor.execute(() -> dispatchSendReady(currentHandler));
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchSendReady(SendReadyHandler currentHandler) {
        InternalAccess.enterCallback();
        try {
            currentHandler.onReady();
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            InternalAccess.leaveCallback();
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
}
