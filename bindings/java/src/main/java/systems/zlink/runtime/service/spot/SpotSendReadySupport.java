/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.sockets.SendReadyHandler;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeCallbackSupport;
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
    private final NativeCallbackSupport callbacks =
        new NativeCallbackSupport("zlink-spot-callback");

    void install(MemorySegment spotHandle, SendReadyHandler nextHandler) {
        Objects.requireNonNull(spotHandle, "spotHandle");
        Objects.requireNonNull(nextHandler, "handler");
        callbacks.ensureNoFailure();
        NativeCallbackSupport.ExecutorLease lease = callbacks.ensureExecutor();
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
                    systems.zlink.contracts.errors.ErrorCategory.HANDLER);
            }
            success = true;
            RuntimeResources.closeArena(callbackArena);
            callbackArena = arena;
            callbackStub = stub;
            handler = nextHandler;
        } finally {
            if (!success) {
                callbacks.clearExecutorIfCreated(lease);
                RuntimeResources.closeArena(arena);
            }
        }
    }

    void ensureNoCallbackFailure() {
        callbacks.ensureNoFailure();
    }

    @Override
    public void close() {
        Arena arena = callbackArena;
        handler = null;
        callbacks.close();
        callbackArena = null;
        callbackStub = MemorySegment.NULL;
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
        ExecutorService executor = callbacks.executor();
        if (currentHandler == null || executor == null)
            return;
        try {
            executor.execute(() -> dispatchSendReady(currentHandler));
        } catch (RejectedExecutionException ex) {
            callbacks.recordFailure(ex);
        } catch (RuntimeException ex) {
            callbacks.recordFailure(ex);
        }
    }

    private void dispatchSendReady(SendReadyHandler currentHandler) {
        InternalAccess.enterCallback();
        try {
            currentHandler.onReady();
        } catch (RuntimeException ex) {
            callbacks.recordFailure(ex);
        } finally {
            InternalAccess.leaveCallback();
        }
    }
}
