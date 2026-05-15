/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.runtime.nativebridge.InternalAccess;
import systems.zlink.runtime.nativebridge.Native;
import systems.zlink.contracts.service.spot.Spot;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;

public final class Timer implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FIRE_HANDLER =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MethodHandle FIRE_HANDLE = callbackHandle();
    private static final MemorySegment FIRE_STUB = LINKER.upcallStub(
      FIRE_HANDLE, FIRE_HANDLER, CALLBACK_ARENA);
    private static final ConcurrentHashMap<Long, Timer> TIMERS =
      new ConcurrentHashMap<>();

    private final boolean ownsHandle;
    private MemorySegment handle;
    private TimerHandler handler;
    private Arena handlerArena;
    private volatile boolean closed;

    public Timer() {
        this(Native.timerNew(), true);
    }

    private Timer(MemorySegment handle, boolean ownsHandle) {
        this.handle = handle;
        this.ownsHandle = ownsHandle;
        if (handle == null || handle.address() == 0) {
            throw new ConfigException(ConfigResult.INVALID_HANDLE);
        }
        TIMERS.put(handle.address(), this);
    }

    public static Timer fromSpot(Spot spot) {
        Objects.requireNonNull(spot, "spot");
        MemorySegment handle = Native.spotTimerNew(InternalAccess.spotHandle(spot));
        return new Timer(handle, true);
    }

    static Timer fromBorrowedHandle(MemorySegment handle) {
        return new Timer(handle, false);
    }

    MemorySegment handle() {
        ensureOpen();
        return handle;
    }

    public void start(Duration interval, long repeatCount) {
        startNanos(toNanos(interval, "interval"), repeatCount);
    }

    void startNanos(long intervalNs, long repeatCount) {
        ensureOpen();
        int rc = Native.timerStart(handle, intervalNs, repeatCount);
        if (rc != 0) {
            throw new ConfigException(ConfigResult.INVALID_ARGUMENT);
        }
    }

    public void stop() {
        ensureOpen();
        int rc = Native.timerStop(handle);
        if (rc != 0) {
            throw new ConfigException(ConfigResult.INVALID_HANDLE);
        }
    }

    public long recv() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fireCount = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.timerRecv(handle, fireCount);
            if (rc == 0) {
                return fireCount.get(ValueLayout.JAVA_LONG, 0);
            }
            throw new RecvException(RecvResult.NO_DATA);
        }
    }

    private static long toNanos(Duration duration, String name) {
        Objects.requireNonNull(duration, name);
        try {
            return duration.toNanos();
        } catch (ArithmeticException ex) {
            throw new IllegalArgumentException(name + " is too large", ex);
        }
    }

    public void onFire(TimerHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        this.handler = handler;
        closeArena(handlerArena);
        handlerArena = Arena.ofShared();
        int rc = Native.timerHandler(handle, FIRE_STUB, MemorySegment.NULL);
        if (rc != 0) {
            throw new HandlerException(HandlerResult.INVALID_HANDLE);
        }
    }

    @Override
    public void close() {
        if (closed || handle == null || handle.address() == 0) {
            return;
        }
        closed = true;
        TIMERS.remove(handle.address(), this);
        if (ownsHandle) {
            Native.timerDestroy(handle);
        }
        handle = MemorySegment.NULL;
        closeArena(handlerArena);
        handlerArena = null;
        handler = null;
    }

    private static MethodHandle callbackHandle() {
        try {
            return MethodHandles.lookup().findStatic(Timer.class,
                "handleFire", MethodType.methodType(void.class,
                    MemorySegment.class, long.class, MemorySegment.class));
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private static void handleFire(MemorySegment timerHandle, long fireCount,
                                   MemorySegment userdata) {
        Timer timer = TIMERS.get(timerHandle.address());
        if (timer == null) {
            return;
        }
        TimerHandler handler = timer.handler;
        if (handler == null) {
            return;
        }
        Socket.enterCallbackContext();
        try {
            handler.onFire(timer, fireCount);
        } finally {
            Socket.leaveCallbackContext();
        }
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0) {
            throw new IllegalStateException("timer is closed");
        }
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive()) {
            arena.close();
        }
    }
}
