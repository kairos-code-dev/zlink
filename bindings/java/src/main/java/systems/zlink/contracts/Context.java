/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import systems.zlink.runtime.nativebridge.Native;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

public final class Context implements AutoCloseable {
    private static final boolean DEBUG_REQREP =
      Boolean.getBoolean("zlink.reqrep.debug");
    private static final int ERRNO_EINTR = 4;
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

    public void recalculateAutoHwm() {
        ensureOpen();
        int rc = Native.ctxAutoHwmRecalculate(handle);
        if (rc != 0) {
            throw new ConfigException(ConfigResult.fromValue(rc));
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        debug("ctxTerm begin");
        while (true) {
            int shutdownRc = Native.ctxShutdown(handle);
            if (shutdownRc == 0 || Native.errno() != ERRNO_EINTR) {
                break;
            }
        }
        while (true) {
            int termRc = Native.ctxTerm(handle);
            if (termRc == 0 || Native.errno() != ERRNO_EINTR) {
                break;
            }
        }
        debug("ctxTerm end");
        handle = MemorySegment.NULL;
    }

    void setOption(ContextOption option, int value) {
        ensureOpen();
        int rc = Native.ctxSet(handle, option.getValue(), value);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_ctx_set");
    }

    void setOptionData(ContextOption option, String value) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment bytes = NativeHelpers.toCString(arena, value);
            int byteLength = bytes.getString(0).getBytes(
                java.nio.charset.StandardCharsets.UTF_8).length;
            int rc = Native.ctxSetData(handle, option.getValue(), bytes,
                byteLength);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_ctx_set_data");
        }
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

    private static void debug(String message) {
        if (DEBUG_REQREP) {
            try {
                Files.writeString(Path.of("/tmp/zlink-reqrep.log"),
                    "[context] " + message + System.lineSeparator(),
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
            } catch (Exception ignored) {
            }
        }
    }
}
