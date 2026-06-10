/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.ForkJoinPool;

/** Options for the poller-backed native receive dispatcher. */
public final class ZlinkDispatchOptions {
    private final int runtimeThreadCount;
    private final Duration pollTimeout;
    private final int maxBatchSize;
    private final Executor callbackExecutor;
    private final boolean failFastOnClosedHandle;

    private ZlinkDispatchOptions(Builder builder) {
        this.runtimeThreadCount = builder.runtimeThreadCount;
        this.pollTimeout = builder.pollTimeout;
        this.maxBatchSize = builder.maxBatchSize;
        this.callbackExecutor = builder.callbackExecutor;
        this.failFastOnClosedHandle = builder.failFastOnClosedHandle;
    }

    public static Builder builder() {
        return new Builder();
    }

    public int runtimeThreadCount() {
        return runtimeThreadCount;
    }

    public Duration pollTimeout() {
        return pollTimeout;
    }

    public int maxBatchSize() {
        return maxBatchSize;
    }

    public Executor callbackExecutor() {
        return callbackExecutor;
    }

    public boolean failFastOnClosedHandle() {
        return failFastOnClosedHandle;
    }

    public static final class Builder {
        private int runtimeThreadCount = 1;
        private Duration pollTimeout = Duration.ofMillis(100);
        private int maxBatchSize = 64;
        private Executor callbackExecutor = ForkJoinPool.commonPool();
        private boolean failFastOnClosedHandle = true;

        private Builder() {
        }

        public Builder runtimeThreadCount(int runtimeThreadCount) {
            if (runtimeThreadCount <= 0) {
                throw new IllegalArgumentException("runtimeThreadCount must be > 0");
            }
            this.runtimeThreadCount = runtimeThreadCount;
            return this;
        }

        public Builder pollTimeout(Duration pollTimeout) {
            if (pollTimeout == null || pollTimeout.isNegative()) {
                throw new IllegalArgumentException("pollTimeout must be non-negative");
            }
            this.pollTimeout = pollTimeout;
            return this;
        }

        public Builder maxBatchSize(int maxBatchSize) {
            if (maxBatchSize <= 0) {
                throw new IllegalArgumentException("maxBatchSize must be > 0");
            }
            this.maxBatchSize = maxBatchSize;
            return this;
        }

        public Builder callbackExecutor(Executor callbackExecutor) {
            this.callbackExecutor = Objects.requireNonNull(callbackExecutor, "callbackExecutor");
            return this;
        }

        public Builder failFastOnClosedHandle(boolean failFastOnClosedHandle) {
            this.failFastOnClosedHandle = failFastOnClosedHandle;
            return this;
        }

        public ZlinkDispatchOptions build() {
            return new ZlinkDispatchOptions(this);
        }
    }
}
