/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.MemorySegment;

public final class Context implements AutoCloseable {
    private MemorySegment handle;

    public Context() {
        this.handle = Native.ctxNew();
        if (handle == null || handle.address() == 0) {
            throw new RuntimeException("zlink_ctx_new failed");
        }
    }

    MemorySegment handle() {
        return handle;
    }

    public void setOption(ContextOption option, int value) {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("context is closed");
        int rc = Native.ctxSet(handle, option.getValue(), value);
        if (rc != 0)
            throw new RuntimeException("zlink_ctx_set failed");
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.ctxTerm(handle);
        handle = MemorySegment.NULL;
    }
}
