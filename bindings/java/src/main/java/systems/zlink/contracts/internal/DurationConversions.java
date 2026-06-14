/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.internal;

import java.time.Duration;

public final class DurationConversions {
    private DurationConversions() {
    }

    public static int toIntMillis(Duration timeout, String name) {
        if (timeout == null) {
            throw new NullPointerException(name);
        }
        long millis = timeout.toMillis();
        if (millis < Integer.MIN_VALUE || millis > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(
                name + " millis out of int range: " + millis);
        }
        return (int) millis;
    }

    public static int timeoutMillis(Duration timeout) {
        if (timeout == null || timeout.isZero()) {
            return 0;
        }
        long millis = Math.max(1L, timeout.toMillis());
        return millis >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) millis;
    }

    public static int toNativeTimeoutMillis(Duration duration, String name) {
        if (duration == null) {
            throw new NullPointerException(name);
        }
        long nanos;
        try {
            nanos = duration.toNanos();
        } catch (ArithmeticException ex) {
            throw new IllegalArgumentException(name + " is too large", ex);
        }
        if (nanos % 1_000_000L != 0L) {
            throw new IllegalArgumentException(
                name + " must be millisecond aligned");
        }
        long millis = nanos / 1_000_000L;
        if (millis < 0 || millis > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(
                name + " is outside native millisecond range");
        }
        return (int) millis;
    }
}
