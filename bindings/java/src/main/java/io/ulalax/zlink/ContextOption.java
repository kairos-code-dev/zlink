/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum ContextOption {
    IO_THREADS(1), MAX_SOCKETS(2), SOCKET_LIMIT(3),
    THREAD_PRIORITY(3), THREAD_SCHED_POLICY(4), MAX_MSGSZ(5),
    MSG_T_SIZE(6), THREAD_AFFINITY_CPU_ADD(7),
    THREAD_AFFINITY_CPU_REMOVE(8), THREAD_NAME_PREFIX(9);

    private final int value;
    ContextOption(int v) { this.value = v; }
    public int getValue() { return value; }
}
