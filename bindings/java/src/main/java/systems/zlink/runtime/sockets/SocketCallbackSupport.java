/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodType;
import java.util.concurrent.ExecutorService;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.runtime.nativeapi.RuntimeResources;

final class SocketCallbackSupport implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();

    private final SocketCore owner;
    private volatile ExecutorService executor;
    private volatile RuntimeException failure;

    SocketCallbackSupport(SocketCore owner) {
        this.owner = owner;
    }

    ExecutorService executor() {
        return executor;
    }

    Arena install(String callbackName, MethodType callbackType,
                  FunctionDescriptor descriptor, String nativeOperation,
                  CallbackRegistration registration) {
        owner.ensureOpen();
        ensureNoFailure();
        ExecutorService currentExecutor = executor;
        boolean createdExecutor = false;
        if (currentExecutor == null) {
            currentExecutor = RuntimeResources.daemonSingleThreadExecutor(
                "zlink-socket-callback");
            executor = currentExecutor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(
            owner.callbackHandle(callbackName, callbackType), descriptor,
            arena);
        boolean success = false;
        try {
            int rc = registration.register(stub);
            if (rc != 0) {
                throw ZlinkException.fromLastError(nativeOperation);
            }
            success = true;
            return arena;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    executor = null;
                    RuntimeResources.shutdownExecutor(currentExecutor);
                }
                RuntimeResources.closeArena(arena);
            }
        }
    }

    void recordFailure(RuntimeException failure) {
        this.failure = failure;
        Thread current = Thread.currentThread();
        Thread.UncaughtExceptionHandler uncaught =
            current.getUncaughtExceptionHandler();
        if (uncaught != null) {
            uncaught.uncaughtException(current, failure);
        }
    }

    void ensureNoFailure() {
        RuntimeException currentFailure = failure;
        if (currentFailure != null) {
            throw currentFailure;
        }
    }

    @Override
    public void close() {
        failure = null;
        RuntimeResources.shutdownExecutor(executor);
        executor = null;
    }

    @FunctionalInterface
    interface CallbackRegistration {
        int register(MemorySegment stub);
    }
}
