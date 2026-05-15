/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.Native;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;

public final class Zlink {
    private Zlink() {}

    static int errno() {
        return Native.errno();
    }

    public static String strerror(int errnum) {
        return Native.strerror(errnum);
    }

    public static boolean has(String capability) {
        Objects.requireNonNull(capability, "capability");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cap = NativeHelpers.toCString(arena, capability);
            return Native.has(cap) != 0;
        }
    }

    public static int[] version() {
        return Native.version();
    }

    public static void proxy(Socket frontend, Socket backend, Socket capture) {
        Objects.requireNonNull(frontend, "frontend");
        Objects.requireNonNull(backend, "backend");
        Native.proxy(frontend.handle(), backend.handle(),
            capture == null ? MemorySegment.NULL : capture.handle());
    }

    public static void proxySteerable(Socket frontend, Socket backend,
                                      Socket capture, Socket control) {
        Objects.requireNonNull(frontend, "frontend");
        Objects.requireNonNull(backend, "backend");
        Objects.requireNonNull(control, "control");
        Native.proxySteerable(frontend.handle(), backend.handle(),
            capture == null ? MemorySegment.NULL : capture.handle(),
            control.handle());
    }

    static void sleep(int seconds) {
        if (seconds < 0)
            throw new IllegalArgumentException("seconds must be >= 0");
        Native.sleep(seconds);
    }

    public static void sleep(Duration duration) {
        Objects.requireNonNull(duration, "duration");
        long seconds = duration.toSeconds();
        if (seconds < 0 || seconds > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(
              "duration seconds out of int range: " + seconds);
        }
        Native.sleep((int) seconds);
    }

    static void multipartClose(Message[] parts) {
        Message.closeAll(parts);
    }
}
