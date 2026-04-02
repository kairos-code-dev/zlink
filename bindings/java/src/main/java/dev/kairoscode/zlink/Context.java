/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.MemorySegment;

public final class Context implements AutoCloseable {
    private MemorySegment handle;

    public Context() {
        this.handle = Native.ctxNew();
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_ctx_new");
    }

    MemorySegment handle() {
        return handle;
    }

    public int ioThreads() {
        return getOption(ContextOption.IO_THREADS);
    }

    public void ioThreads(int count) {
        setOption(ContextOption.IO_THREADS, count);
    }

    public int maxSockets() {
        return getOption(ContextOption.MAX_SOCKETS);
    }

    public void maxSockets(int count) {
        setOption(ContextOption.MAX_SOCKETS, count);
    }

    public int socketLimit() {
        return getOption(ContextOption.SOCKET_LIMIT);
    }

    public int threadPriority() {
        return getOption(ContextOption.THREAD_PRIORITY);
    }

    public void threadPriority(int priority) {
        setOption(ContextOption.THREAD_PRIORITY, priority);
    }

    public int threadSchedPolicy() {
        return getOption(ContextOption.THREAD_SCHED_POLICY);
    }

    public void threadSchedPolicy(int policy) {
        setOption(ContextOption.THREAD_SCHED_POLICY, policy);
    }

    public int maxMessageSize() {
        return getOption(ContextOption.MAX_MSGSZ);
    }

    public void maxMessageSize(int bytes) {
        setOption(ContextOption.MAX_MSGSZ, bytes);
    }

    public int messageStructSize() {
        return getOption(ContextOption.MSG_T_SIZE);
    }

    public boolean blocky() {
        return getOption(ContextOption.BLOCKY) != 0;
    }

    public void blocky(boolean enabled) {
        setOption(ContextOption.BLOCKY, enabled ? 1 : 0);
    }

    public void addThreadAffinityCpu(int cpu) {
        setOption(ContextOption.THREAD_AFFINITY_CPU_ADD, cpu);
    }

    public void removeThreadAffinityCpu(int cpu) {
        setOption(ContextOption.THREAD_AFFINITY_CPU_REMOVE, cpu);
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
