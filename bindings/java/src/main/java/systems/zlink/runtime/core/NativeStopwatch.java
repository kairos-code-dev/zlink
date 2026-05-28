/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.core;

import systems.zlink.contracts.core.Stopwatch;

import systems.zlink.contracts.errors.ConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.runtime.nativeapi.Native;
import java.lang.foreign.MemorySegment;
import java.time.Duration;

final class NativeStopwatch implements Stopwatch {
    private MemorySegment handle;
    private boolean stopped;

    NativeStopwatch() {
        this.handle = Native.stopwatchStart();
        if (handle == null || handle.address() == 0) {
            throw new ConfigException(ConfigResult.INVALID_HANDLE);
        }
    }

    public Duration intermediate() {
        ensureOpen();
        return Duration.ofNanos(Native.stopwatchIntermediate(handle));
    }

    public Duration stop() {
        ensureOpen();
        long elapsed = Native.stopwatchStop(handle);
        handle = MemorySegment.NULL;
        stopped = true;
        return Duration.ofNanos(elapsed);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0) {
            return;
        }
        if (!stopped) {
            stop();
        } else {
            handle = MemorySegment.NULL;
        }
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0) {
            throw new IllegalStateException("stopwatch is closed");
        }
    }
}
