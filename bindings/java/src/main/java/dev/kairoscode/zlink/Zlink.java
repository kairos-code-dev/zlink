/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
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

    public static void sleep(int seconds) {
        if (seconds < 0)
            throw new IllegalArgumentException("seconds must be >= 0");
        Native.sleep(seconds);
    }

    public static void multipartClose(Message[] parts) {
        Message.closeAll(parts);
    }
}
