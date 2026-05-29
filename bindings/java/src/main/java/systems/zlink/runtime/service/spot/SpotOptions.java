/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.Objects;

final class SpotOptions {
    private static final int OPT_REQUEST_TIMEOUT_MS = 0x3701;

    private final Spot spot;

    public SpotOptions(Spot spot) {
        this.spot = spot;
    }

    public Duration requestTimeout() {
        return Duration.ofMillis(getIntOption(OPT_REQUEST_TIMEOUT_MS));
    }

    public void requestTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        setIntOption(OPT_REQUEST_TIMEOUT_MS, toIntMillis(value, "value"));
    }

    private int getIntOption(int option) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSpotOption(InternalAccess.spotHandle(spot), option, nativeValue,
              len);
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            return nativeValue.get(ValueLayout.JAVA_INT, 0);
        }
    }

    private void setIntOption(int option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            nativeValue.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.setSpotOption(InternalAccess.spotHandle(spot), option, nativeValue,
              ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
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
