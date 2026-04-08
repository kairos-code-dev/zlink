/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

/**
 * Typed facade for context options.
 */
public final class ContextOptions {
    private final Context context;

    ContextOptions(Context context) {
        this.context = context;
    }

    public int ioThreads() {
        return context.getOption(ContextOption.IO_THREADS);
    }

    public void ioThreads(int count) {
        context.setOption(ContextOption.IO_THREADS, count);
    }

    public int maxSockets() {
        return context.getOption(ContextOption.MAX_SOCKETS);
    }

    public void maxSockets(int count) {
        context.setOption(ContextOption.MAX_SOCKETS, count);
    }

    public int socketLimit() {
        return context.getOption(ContextOption.SOCKET_LIMIT);
    }

    public int threadPriority() {
        return context.getOption(ContextOption.THREAD_PRIORITY);
    }

    public void threadPriority(int priority) {
        context.setOption(ContextOption.THREAD_PRIORITY, priority);
    }

    public int threadSchedulingPolicy() {
        return context.getOption(ContextOption.THREAD_SCHED_POLICY);
    }

    public void threadSchedulingPolicy(int policy) {
        context.setOption(ContextOption.THREAD_SCHED_POLICY, policy);
    }

    public int maxMsgSize() {
        return context.getOption(ContextOption.MAX_MSGSZ);
    }

    public void maxMsgSize(int bytes) {
        context.setOption(ContextOption.MAX_MSGSZ, bytes);
    }

    public int msgTSize() {
        return context.getOption(ContextOption.MSG_T_SIZE);
    }

    public boolean blocky() {
        return context.getOption(ContextOption.BLOCKY) != 0;
    }

    public void blocky(boolean enabled) {
        context.setOption(ContextOption.BLOCKY, enabled ? 1 : 0);
    }

    public void addThreadAffinity(int cpu) {
        context.setOption(ContextOption.THREAD_AFFINITY_CPU_ADD, cpu);
    }

    public void removeThreadAffinity(int cpu) {
        context.setOption(ContextOption.THREAD_AFFINITY_CPU_REMOVE, cpu);
    }
}
