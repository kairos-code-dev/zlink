/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.MemorySegment;

public final class Context implements AutoCloseable {
    private final ContextOptions options;
    private MemorySegment handle;

    public Context() {
        this.handle = Native.ctxNew();
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_ctx_new");
        this.options = new ContextOptions(this);
    }

    public ContextOptions options() {
        return options;
    }

    MemorySegment handle() {
        return handle;
    }

    public void shutdown() {
        ensureOpen();
        int rc = Native.ctxShutdown(handle);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_ctx_shutdown");
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.ctxTerm(handle);
        handle = MemorySegment.NULL;
    }

    void setOption(ContextOption option, int value) {
        ensureOpen();
        int rc = Native.ctxSet(handle, option.getValue(), value);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_ctx_set");
    }

    int getOption(ContextOption option) {
        ensureOpen();
        int rc = Native.ctxGet(handle, option.getValue());
        if (rc < 0 && option != ContextOption.THREAD_PRIORITY
            && option != ContextOption.THREAD_SCHED_POLICY)
            throw ZlinkException.fromLastError("zlink_ctx_get");
        return rc;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("context is closed");
    }
}
