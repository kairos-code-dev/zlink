/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.Socket;
import java.util.List;
import java.util.Objects;

public final class PerfSocketPollSet implements AutoCloseable {
    private final Poller poller;
    private final Socket[] sockets;
    private final int[] events;
    private final int[] ready;

    private PerfSocketPollSet(List<Socket> sockets, int initialEvents) {
        this.poller = new Poller();
        this.sockets = sockets.toArray(Socket[]::new);
        this.events = new int[this.sockets.length];
        this.ready = new int[this.sockets.length];
        for (int i = 0; i < this.sockets.length; i++) {
            setEvents(i, initialEvents);
            poller.add(this.sockets[i], initialEvents);
        }
    }

    public static PerfSocketPollSet fromSockets(List<Socket> sockets,
                                                int initialEvents) {
        Objects.requireNonNull(sockets, "sockets");
        return new PerfSocketPollSet(sockets, initialEvents);
    }

    public void setEvents(int index, int newEvents) {
        checkIndex(index);
        events[index] = newEvents;
        poller.modify(sockets[index], newEvents);
    }

    public boolean isReady(int index, int eventMask) {
        checkIndex(index);
        return (ready[index] & eventMask) != 0;
    }

    public int poll(int timeoutMs) {
        clearReady();
        int count = poller.pollCount(timeoutMs);
        for (int i = 0; i < count; i++) {
            Socket socket = poller.readySocket(i);
            if (socket == null) {
                continue;
            }
            for (int j = 0; j < sockets.length; j++) {
                if (sockets[j] == socket) {
                    ready[j] |= poller.readyRevents(i) & 0xFFFF;
                    break;
                }
            }
        }
        return count;
    }

    @Override
    public void close() {
        poller.close();
    }

    private void clearReady() {
        for (int i = 0; i < ready.length; i++) {
            ready[i] = 0;
        }
    }

    private void checkIndex(int index) {
        if (index < 0 || index >= sockets.length) {
            throw new IndexOutOfBoundsException("index " + index);
        }
    }
}
