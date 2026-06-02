package systems.zlink.framework.spots;

public final class ZLinkTimerOptions {
    private ZLinkTimerOverrunPolicy overrunPolicy = ZLinkTimerOverrunPolicy.SKIP_LATE_TICKS;
    private int maxCatchUpTicks = 1;
    private boolean stopOnUnhandledException = true;

    public ZLinkTimerOverrunPolicy overrunPolicy() {
        return overrunPolicy;
    }

    public void setOverrunPolicy(ZLinkTimerOverrunPolicy overrunPolicy) {
        this.overrunPolicy = overrunPolicy;
    }

    public int maxCatchUpTicks() {
        return maxCatchUpTicks;
    }

    public void setMaxCatchUpTicks(int maxCatchUpTicks) {
        this.maxCatchUpTicks = maxCatchUpTicks;
    }

    public boolean stopOnUnhandledException() {
        return stopOnUnhandledException;
    }

    public void setStopOnUnhandledException(boolean stopOnUnhandledException) {
        this.stopOnUnhandledException = stopOnUnhandledException;
    }
}
