package systems.zlink.e2e.discoveryregistryha.consumer;

import java.time.Duration;

final class LocationStoreDelayState {
    private volatile int delayMilliseconds;

    int delayMilliseconds() {
        return delayMilliseconds;
    }

    void setDelay(Duration delay) {
        long clamped = Math.max(0, Math.min(5000, delay.toMillis()));
        delayMilliseconds = (int) clamped;
    }
}
