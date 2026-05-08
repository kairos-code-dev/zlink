/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.ConfigException;
import systems.zlink.ConfigResult;
import systems.zlink.internal.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.Objects;

final class SpotOptions {
    private static final int OPT_REQUEST_TIMEOUT_MS = 0x3701;

    private final Spot spot;

    SpotOptions(Spot spot) {
        this.spot = spot;
    }

    Duration requestTimeout() {
        return Duration.ofMillis(getIntOption(OPT_REQUEST_TIMEOUT_MS));
    }

    void requestTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        setIntOption(OPT_REQUEST_TIMEOUT_MS, toIntMillis(value, "value"));
    }

    private int getIntOption(int option) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSpotOption(spot.handle(), option, nativeValue,
              len);
            if (rc != 0)
                throw new ConfigException(ConfigResult.fromValue(rc));
            return nativeValue.get(ValueLayout.JAVA_INT, 0);
        }
    }

    private void setIntOption(int option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            nativeValue.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.setSpotOption(spot.handle(), option, nativeValue,
              ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new ConfigException(ConfigResult.fromValue(rc));
        }
    }

    private static int toIntMillis(Duration timeout, String name) {
        long millis = timeout.toMillis();
        if (millis < Integer.MIN_VALUE || millis > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(name + " millis out of int range: "
                + millis);
        }
        return (int) millis;
    }
}
