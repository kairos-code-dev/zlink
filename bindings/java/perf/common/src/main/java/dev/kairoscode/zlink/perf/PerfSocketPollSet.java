/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.ZlinkException;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

public final class PerfSocketPollSet implements AutoCloseable {
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private final Socket[] sockets;
    private final int[] readyMasks;
    private final Poller poller = new Poller();
    private final Map<Socket, Integer> indices = new IdentityHashMap<>();

    private PerfSocketPollSet(List<Socket> sockets, int initialEvents) {
        this.sockets = sockets.toArray(Socket[]::new);
        this.readyMasks = new int[this.sockets.length];
        for (int i = 0; i < this.sockets.length; i++) {
            Socket socket = Objects.requireNonNull(this.sockets[i], "socket");
            poller.add(socket, initialEvents);
            indices.put(socket, i);
        }
    }

    public static PerfSocketPollSet fromSockets(List<Socket> sockets,
                                                int initialEvents) {
        Objects.requireNonNull(sockets, "sockets");
        return new PerfSocketPollSet(sockets, initialEvents);
    }

    public void setEvents(int index, int newEvents) {
        checkIndex(index);
        poller.modify(sockets[index], newEvents);
    }

    public boolean isReady(int index, int eventMask) {
        checkIndex(index);
        return (readyMasks[index] & eventMask) != 0;
    }

    public int poll(int timeoutMs) {
        clearReadyMasks();
        int rc;
        try {
            rc = poller.pollCount(timeoutMs);
        } catch (ZlinkException ex) {
            int errno = ex.getInternalErrno();
            if (errno == ERRNO_EINTR
                || errno == ERRNO_EAGAIN
                || errno == ERRNO_EWOULDBLOCK_WIN) {
                return 0;
            }
            throw ex;
        }
        for (int i = 0; i < rc; i++) {
            Socket socket = poller.readySocket(i);
            Integer index = socket == null ? null : indices.get(socket);
            if (index != null) {
                readyMasks[index] = poller.readyRevents(i) & 0xFFFF;
            }
        }
        return rc;
    }

    @Override
    public void close() {
        poller.close();
    }

    private void clearReadyMasks() {
        for (int i = 0; i < readyMasks.length; i++) {
            readyMasks[i] = 0;
        }
    }

    private void checkIndex(int index) {
        if (index < 0 || index >= sockets.length) {
            throw new IndexOutOfBoundsException("index " + index);
        }
    }
}
