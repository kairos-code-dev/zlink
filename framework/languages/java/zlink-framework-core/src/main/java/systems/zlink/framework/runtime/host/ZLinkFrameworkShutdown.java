package systems.zlink.framework.runtime.host;

import systems.zlink.contracts.errors.ZlinkCloseException;

final class ZLinkFrameworkShutdown {
    private final java.util.ArrayDeque<Runnable> actions = new java.util.ArrayDeque<>();

    void defer(Runnable action) {
        actions.push(action);
    }

    void close() {
        RuntimeException failure = null;
        while (!actions.isEmpty()) {
            try {
                actions.pop().run();
            } catch (ZlinkCloseException ignored) {
            } catch (RuntimeException ex) {
                if (failure == null) {
                    failure = ex;
                } else {
                    failure.addSuppressed(ex);
                }
            }
        }
        if (failure != null) {
            throw failure;
        }
    }
}
