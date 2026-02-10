/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
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

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.ctxTerm(handle);
        handle = MemorySegment.NULL;
    }
}
