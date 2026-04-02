/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Objects;

public final class Zlink {
    private Zlink() {}

    public static int errno() {
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

    static void sleep(int seconds) {
        if (seconds < 0)
            throw new IllegalArgumentException("seconds must be >= 0");
        Native.sleep(seconds);
    }
}
