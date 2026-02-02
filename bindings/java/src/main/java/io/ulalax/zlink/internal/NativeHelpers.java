package io.ulalax.zlink.internal;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;

public final class NativeHelpers {
    private NativeHelpers() {}

    public static MemorySegment toCString(Arena arena, String value) {
        return arena.allocateFrom(value, StandardCharsets.UTF_8);
    }

    public static String fromCString(MemorySegment segment, int maxLen) {
        if (segment == null || segment.address() == 0)
            return "";
        byte[] bytes = segment.asSlice(0, maxLen).toArray(ValueLayout.JAVA_BYTE);
        int len = 0;
        while (len < bytes.length && bytes[len] != 0)
            len++;
        return new String(bytes, 0, len, StandardCharsets.UTF_8);
    }
}
